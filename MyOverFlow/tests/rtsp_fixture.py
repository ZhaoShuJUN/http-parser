"""Minimal local RTSP/TCP test fixture; deliberately not a production RTSP server."""
import base64
import re
import socket
import struct
import threading
import time


class RtspFixture:
    def __init__(self, h264):
        self.nals = [n for n in re.split(b'\x00\x00\x00?\x01', h264) if n]
        sps = next(n for n in self.nals if n[0] & 31 == 7)
        pps = next(n for n in self.nals if n[0] & 31 == 8)
        self.sprop = base64.b64encode(sps).decode() + ',' + base64.b64encode(pps).decode()
        self.profile = sps[1:4].hex()
        self.frames = []
        frame = []
        for n in self.nals:
            if n[0] & 31 == 9 and frame:
                self.frames.append(frame)
                frame = []
            frame.append(n)
        if frame:
            self.frames.append(frame)
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1', 0))
        self.socket.listen(20)
        self.socket.settimeout(.2)
        self.port = self.socket.getsockname()[1]
        self.done = threading.Event()
        self.clients = []
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.done.is_set():
            try:
                s, _ = self.socket.accept()
            except socket.timeout:
                continue
            self.clients.append(s)
            threading.Thread(target=self.serve, args=(s,), daemon=True).start()

    def serve(self, s):
        lock = threading.Lock()
        playing = threading.Event()
        ended = threading.Event()

        def write(b):
            with lock:
                s.sendall(b)

        def media():
            seq, ts = 0, 0
            deadline = time.monotonic()
            try:
                while not ended.is_set() and not self.done.is_set():
                    for frame in self.frames:
                        if ended.is_set() or self.done.is_set():
                            return
                        for k, nal in enumerate(frame):
                            parts = [nal] if len(nal) <= 1200 else [
                                bytes([(nal[0] & 0xe0) | 28,
                                       (nal[0] & 31) | (0x80 if off == 1 else 0) |
                                       (0x40 if off + 1198 >= len(nal) else 0)]) + nal[off:off + 1198]
                                for off in range(1, len(nal), 1198)]
                            for j, payload in enumerate(parts):
                                marker = k == len(frame) - 1 and j == len(parts) - 1
                                rtp = struct.pack('!BBHII', 0x80, 96 | (128 if marker else 0),
                                                  seq, ts, 0x10203040) + payload
                                write(b'$\x00' + struct.pack('!H', len(rtp)) + rtp)
                                seq = (seq + 1) & 65535
                        ts = (ts + 3600) & 0xffffffff
                        deadline += .04
                        ended.wait(max(0, deadline - time.monotonic()))
            except (OSError, ConnectionError):
                pass
        try:
            b = b''
            while not self.done.is_set():
                d = s.recv(65536)
                if not d:
                    break
                b += d
                while b:
                    if b[0] == 36:  # RTCP interleaved input
                        if len(b) < 4:
                            break
                        n = int.from_bytes(b[2:4], 'big') + 4
                        if len(b) < n:
                            break
                        b = b[n:]
                        continue
                    if b'\r\n\r\n' not in b:
                        break
                    head, b = b.split(b'\r\n\r\n', 1)
                    lines = head.decode().split('\r\n')
                    method, url, _ = lines[0].split(' ')
                    headers = dict((k.lower(), v.strip()) for k, v in
                                   (l.split(':', 1) for l in lines[1:]))
                    out = f'RTSP/1.0 200 OK\r\nCSeq: {headers["cseq"]}\r\nSession: fixture;timeout=60\r\n'
                    body = b''
                    if method == 'OPTIONS':
                        out += 'Public: OPTIONS, DESCRIBE, SETUP, PLAY, GET_PARAMETER, TEARDOWN\r\n'
                    elif method == 'DESCRIBE':
                        body = ('v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=Fixture\r\nt=0 0\r\n'
                                'a=control:*\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n'
                                f'a=fmtp:96 packetization-mode=1;profile-level-id={self.profile};sprop-parameter-sets={self.sprop}\r\n'
                                'a=framerate:25\r\na=control:trackID=0\r\n').encode()
                        out += f'Content-Type: application/sdp\r\nContent-Base: rtsp://127.0.0.1:{self.port}/test/\r\n'
                    elif method == 'SETUP':
                        out += 'Transport: RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=10203040\r\n'
                    elif method == 'PLAY':
                        out += 'Range: npt=0.000-\r\n'
                    write(out.encode() + f'Content-Length: {len(body)}\r\n\r\n'.encode() + body)
                    if method == 'PLAY' and not playing.is_set():
                        playing.set()
                        threading.Thread(target=media, daemon=True).start()
                    if method == 'TEARDOWN':
                        return
        except (OSError, ConnectionError):
            pass
        finally:
            ended.set()
            s.close()

    def close(self):
        self.done.set()
        self.thread.join(timeout=1)
        for s in self.clients:
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            s.close()
        self.socket.close()
