#!/usr/bin/env python3
"""Local integration: 8 PTY buses, HTTP auth, MQTT QoS1/reconnect, ISAPI,
SIP Digest/catalog/INVITE/ACK/BYE, real H.264 fixture -> PS/RTP -> ffprobe.
No physical sensors, cameras, public cloud or GB platform are contacted.
"""
import argparse
import base64
import hashlib
import http.server
import json
import os
import pathlib
import pty
import queue
import re
import select
import socket
import sqlite3
import struct
import subprocess
import tempfile
import threading
import time
import configparser
from rtsp_fixture import RtspFixture


def crc(b):
    c = 0xffff
    for x in b:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ (0xa001 if c & 1 else 0)
    return struct.pack('<H', c)


def sip_parse(b):
    h, body = b.split(b'\r\n\r\n', 1)
    lines = h.decode().split('\r\n')
    return lines[0], {k.lower(): v.strip() for k, v in (line.split(':', 1) for line in lines[1:])}, body


def write_ini(cfg, path):
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini['Monitor'] = {'DataDir': cfg['dataDir']}
    ini['UartCfg'] = {'UartNumb': str(len(cfg['ports']))}
    for i, p in enumerate(cfg['ports'], 1):
        ini['UartCfg'].update({f'UartPort{i}': p['device'], f'UartBaud{i}': str(p['baud']),
            f'UartEffe{i}': '0', f'UartStop{i}': '0', f'TimeoutMs{i}': str(p['timeoutMs']), f'Retries{i}': str(p['retries'])})
    ini['DeviceCfg'] = {'DeviceHaveNumb': str(len(cfg['sensors']))}
    for i, s in enumerate(cfg['sensors'], 1):
        ini['DeviceCfg'].update({f'DevicePort{i}': str(s['port']), f'DeviceAddr{i}': str(s['slave']),
            f'DeviceRegStart{i}': str(s['register']), f'DeviceRegEnd{i}': '1'})
        ini[f'Sensor{i}'] = {key: str(s.get(k, default)) for key, k, default in [
            ('Id', 'id', ''), ('Name', 'name', ''), ('Category', 'category', '温度'), ('Unit', 'unit', ''),
            ('Format', 'format', 'u16'), ('WordOrder', 'wordOrder', 'ABCD'), ('Scale', 'scale', 1),
            ('Low', 'low', 0), ('High', 'high', 40), ('Hysteresis', 'hysteresis', 1),
            ('IntervalMs', 'intervalMs', 200), ('Camera', 'camera', 0), ('Preset', 'preset', 1),
            ('LocalId', 'localId', ''), ('DeviceId', 'deviceId', ''), ('NodeId', 'nodeId', '')]}
    ini['CameraCfg'] = {'Count': str(len(cfg['cameras']))}
    for i, c in enumerate(cfg['cameras'], 1):
        ini[f'Camera{i}'] = {key: str(c[k]).lower() if isinstance(c[k], bool) else str(c[k])
            for key, k in [('Enabled','enabled'), ('Name','name'), ('Rtsp','rtsp'), ('Isapi','isapi'),
                ('User','user'), ('PasswordEnv','passwordEnv'), ('PtzChannel','channel'), ('ChannelId','channelId')]}
    for group, key in [('Mqtt','mqtt'), ('Recording','recording'), ('GB28181','gb28181')]:
        ini[group] = {k: str(v).lower() if isinstance(v, bool) else str(v) for k, v in cfg[key].items()}
    with path.open('w') as f:
        ini.write(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary')
    ap.add_argument('--media-binary')
    ap.add_argument('--qos', type=int, choices=(0, 1), default=1)
    ap.add_argument('--stall-one-bus', action='store_true')
    args = ap.parse_args()
    binary = str(pathlib.Path(args.binary).resolve())
    root = pathlib.Path(__file__).resolve().parents[1]
    errors = queue.Queue()
    done = threading.Event()
    threads = []
    counts = {'auth': 0, 'mqtt': 0, 'topo': 0, 'datas': 0, 'ptz': 0, 'register': 0,
              'catalog': 0, 'rtp': 0, 'bye': 0, 'serial': [0] * 8}

    def worker(fn):
        def run():
            try:
                fn()
            except Exception as e:
                if not done.is_set():
                    errors.put((fn.__name__, repr(e)))
        t = threading.Thread(target=run, daemon=True)
        threads.append(t)
        t.start()

    with tempfile.TemporaryDirectory(prefix='monitor-integration-') as tmp:
        tmp = pathlib.Path(tmp)
        cfg = json.loads((root / 'tests/fixture-settings.json').read_text())
        cfg['dataDir'] = str(tmp / 'data')
        masters, slaves = [], []
        addresses = [1, 2, 3, 4, 5, 6, 200, 247]
        cfg['ports'], cfg['sensors'] = [], []
        for i in range(8):
            m, s = pty.openpty()
            masters.append(m)
            slaves.append(s)
            cfg['ports'].append({'device': os.ttyname(s), 'baud': 9600, 'timeoutMs': 300,
                                 'retries': 1, 'parity': 'N', 'stopBits': 1})
            sensor = dict(json.loads((root / 'tests/fixture-settings.json').read_text())['sensors'][0])
            sensor.update(id=f't{i}', name=f'Test {i}', port=i, intervalMs=200, slave=addresses[i],
                          deviceId=f'Lt{i}', nodeId=f't{i}', localId=f'test_{i}', scale=1, high=40, low=0)
            cfg['sensors'].append(sensor)

            def serial(m=m, i=i):
                b = b''
                while not done.is_set():
                    if not select.select([m], [], [], .1)[0]:
                        continue
                    b += os.read(m, 1024)
                    while len(b) >= 8:
                        req, b = b[:8], b[8:]
                        assert crc(req[:-2]) == req[-2:] and req[0] == addresses[i]
                        assert req[1] == 3 and req[2:6] == b'\x00\x00\x00\x01'
                        counts['serial'][i] += 1
                        n = counts['serial'][i]
                        if n == 3 or (args.stall_one_bus and i == 0 and 5 <= n <= 10):  # other buses must continue
                            continue
                        value = 50 if (n // 4) % 2 == 0 else 20
                        frame = bytes([addresses[i], 3, 2]) + struct.pack('>H', value)
                        frame += crc(frame)
                        if n == 1:  # noise + bad CRC then fragmented valid response
                            os.write(m, b'\x00\xff' + frame[:-1] + bytes([frame[-1] ^ 1]))
                        os.write(m, frame[:3])
                        time.sleep(.008)
                        os.write(m, frame[3:])
            worker(serial)

        mqtt_sock = socket.socket()
        mqtt_sock.bind(('127.0.0.1', 0))
        mqtt_sock.listen()
        mqtt_sock.settimeout(.2)

        def exact(s, n):
            b = b''
            while len(b) < n:
                d = s.recv(n - len(b))
                if not d:
                    raise EOFError()
                b += d
            return b

        def mqtt():
            while not done.is_set():
                try:
                    s, _ = mqtt_sock.accept()
                except socket.timeout:
                    continue
                s.settimeout(2)
                counts['mqtt'] += 1
                topology = False
                with s:
                    try:
                        while not done.is_set():
                            kind = exact(s, 1)[0]
                            n, mul = 0, 1
                            while True:
                                d = exact(s, 1)[0]
                                n += (d & 127) * mul
                                mul *= 128
                                if not d & 128:
                                    break
                            b = exact(s, n)
                            if kind >> 4 == 1:
                                assert b.startswith(b'\x00\x04MQTT\x04')
                                s.sendall(b'\x20\x02\x00\x00')
                            elif kind >> 4 == 3:
                                assert (kind >> 1) & 3 == args.qos
                                length = int.from_bytes(b[:2], 'big')
                                topic = b[2:2 + length].decode()
                                mid = b[2 + length:4 + length] if args.qos else b''
                                data = json.loads(b[4 + length:] if args.qos else b[2 + length:])
                                if topic.endswith('/topo/add'):
                                    assert topic == '/v1/devices/test-gateway/topo/add'
                                    assert len(data['deviceInfo']) == 8
                                    topology = True
                                    counts['topo'] += 1
                                else:
                                    assert topology and topic == '/v1/devices/test-gateway/datas'
                                    assert len(data['devices']) >= 8
                                    assert any(x['data'][0]['valid'] for x in data['devices'])
                                    counts['datas'] += 1
                                if args.qos:
                                    s.sendall(b'\x40\x02' + mid)
                                if counts['mqtt'] == 1 and counts['datas'] == 1:
                                    break  # force client reauthentication and topology rebuild
                            elif kind >> 4 == 12:
                                s.sendall(b'\xd0\x00')
                    except (EOFError, ConnectionResetError, socket.timeout):
                        pass
        worker(mqtt)

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                assert self.path == '/iot/devices/auth/gateway'
                data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                assert data['gatewaySN'] == 'test-sn' and data['checkID'] == 'integration-check'
                counts['auth'] += 1
                body = json.dumps({'gatewayId': 'test-gateway', 'protocols': [
                    {'protocol': 'mqtt', 'addr': f'127.0.0.1:{mqtt_sock.getsockname()[1]}'}]}).encode()
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_PUT(self):
                self.rfile.read(int(self.headers.get('Content-Length', 0)))
                auth = self.headers.get('Authorization')
                if auth != 'Basic ' + base64.b64encode(b'admin:camera-test').decode():
                    self.send_response(401)
                    self.send_header('WWW-Authenticate', 'Basic realm="CameraTest"')
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                    return
                assert self.path.startswith('/ISAPI/PTZCtrl/channels/1/presets/1')
                counts['ptz'] += 1
                b = b'<ResponseStatus><statusCode>1</statusCode></ResponseStatus>'
                self.send_response(200)
                self.send_header('Content-Length', str(len(b)))
                self.end_headers()
                self.wfile.write(b)

        httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        worker(httpd.serve_forever)
        http_port = httpd.server_address[1]
        cfg['mqtt'].update(enabled=True, authUrl=f'http://127.0.0.1:{http_port}/iot/devices/auth/gateway',
                           gatewaySN='test-sn', checkIdEnv='INTEGRATION_CHECK', reportMs=1000, qos=args.qos)
        cfg['cameras'][0].update(enabled=True, rtsp='rtsp://127.0.0.1:8554/test',
                                 isapi=f'http://127.0.0.1:{http_port}', passwordEnv='INTEGRATION_CAMERA')

        # Produce actual H.264, with access-unit delimiters, SPS/PPS and no B frames.
        fixture = tmp / 'fixture.h264'
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                        'testsrc2=size=160x120:rate=25', '-t', '2', '-c:v', 'libx264', '-bf', '0',
                        '-g', '25', '-bsf:v', 'h264_metadata=aud=insert', '-f', 'h264', str(fixture)], check=True)
        rtsp = RtspFixture(fixture.read_bytes())
        for camera in cfg['cameras']:
            camera.update(enabled=True, rtsp=f'rtsp://127.0.0.1:{rtsp.port}/test',
                          isapi=f'http://127.0.0.1:{http_port}', passwordEnv='INTEGRATION_CAMERA')
        recording_dir = tmp / 'disk'
        recording_dir.mkdir()
        cfg['recording'].update(enabled=True, directory=str(recording_dir), requireMount=False,
                                maxGiB=.1, reserveMiB=0, segmentSeconds=5, ffmpeg='ffmpeg')
        sip = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sip.bind(('127.0.0.1', 0))
        sip.settimeout(.2)
        local = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        local.bind(('127.0.0.1', 0))
        local_port = local.getsockname()[1]
        local.close()
        rtp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        rtp.bind(('127.0.0.1', 0))
        rtp.settimeout(.2)
        cfg['gb28181'].update(enabled=True, localAddress='127.0.0.1', localPort=local_port,
                             serverAddress='127.0.0.1', serverPort=sip.getsockname()[1],
                             passwordEnv='INTEGRATION_GB')
        peer = ('127.0.0.1', local_port)
        device = cfg['gb28181']['deviceId']
        channel = cfg['cameras'][0]['channelId']
        ps_file = tmp / 'received.ps'
        channel2 = cfg['cameras'][1]['channelId']
        ps_file2 = tmp / 'received2.ps'

        def sip_request(method, target, seq, call, body=b'', to_tag=''):
            headers = (f'{method} sip:{target}@3402000000 SIP/2.0\r\n'
                       f'Via: SIP/2.0/UDP 127.0.0.1:{sip.getsockname()[1]};branch=z9hG4bKtest{seq}\r\n'
                       'From: <sip:34020000002000000000@3402000000>;tag=server\r\n'
                       f'To: <sip:{target}@3402000000>{to_tag}\r\nCall-ID: {call}\r\n'
                       f'CSeq: {seq} {method}\r\nContent-Type: application/sdp\r\n'
                       f'Content-Length: {len(body)}\r\n\r\n').encode()
            sip.sendto(headers + body, peer)

        def sip_reply(headers, code=200, extra=''):
            b = f'SIP/2.0 {code} Result\r\n'
            for k in ('via', 'from', 'to', 'call-id', 'cseq'):
                b += f'{k}: {headers[k]}\r\n'
            sip.sendto((b + extra + 'Content-Length: 0\r\n\r\n').encode(), peer)

        def gb():
            invited = False
            while not done.is_set():
                try:
                    b, _ = sip.recvfrom(65535)
                except socket.timeout:
                    continue
                first, h, body = sip_parse(b)
                if first.startswith('REGISTER'):
                    if 'authorization' not in h:
                        sip_reply(h, 401, 'WWW-Authenticate: Digest realm="3402000000", nonce="testnonce", algorithm=MD5, qop="auth"\r\n')
                    else:
                        fields = dict((m[0], m[1] or m[2]) for m in re.findall(r'(\w+)=(?:"([^"]*)"|([^, ]+))', h['authorization']))
                        md5 = lambda s: hashlib.md5(s.encode()).hexdigest()
                        expected = md5(md5(f'{device}:3402000000:gb-test') + ':testnonce:' + fields['nc'] + ':' + fields['cnonce'] + ':auth:' + md5('REGISTER:' + fields['uri']))
                        assert fields['response'] == expected
                        counts['register'] += 1
                        sip_reply(h, extra='Expires: 5\r\n')
                        xml = f'<Query><CmdType>Catalog</CmdType><SN>1</SN><DeviceID>{device}</DeviceID></Query>'.encode()
                        sip_request('MESSAGE', device, 1, 'catalog-call', xml)
                elif first.startswith('MESSAGE'):
                    sip_reply(h)
                    if b'<CmdType>Catalog</CmdType>' in body:
                        assert channel.encode() in body and b'<SumNum>8</SumNum>' in body
                        assert cfg['cameras'][0]['name'].encode('gb2312') in body
                        assert b'encoding="GB2312"' in body
                        counts['catalog'] += 1
                        if not invited:
                            invited = True
                            for index, target in enumerate([channel, channel2]):
                                sdp = (f'v=0\r\no=server 0 0 IN IP4 127.0.0.1\r\ns=Play\r\nc=IN IP4 127.0.0.1\r\n'
                                       f't=0 0\r\nm=video {rtp.getsockname()[1]} RTP/AVP 96\r\na=recvonly\r\na=rtpmap:96 PS/90000\r\ny={123456789+index:010d}\r\n').encode()
                                sip_request('INVITE', target, 2+index*2, f'video-call-{index}', sdp)
                elif first.startswith('SIP/2.0 200') and h['cseq'].endswith(' INVITE'):
                    assert b'a=sendonly' in body
                    index = int(h['call-id'].rsplit('-', 1)[1])
                    target = [channel, channel2][index]
                    assert target.encode() in body
                    tag = ';tag=' + h['to'].split(';tag=')[1]
                    sip_request('ACK', target, 2+index*2, h['call-id'], to_tag=tag)
                elif first.startswith('SIP/2.0 200') and h['cseq'].endswith(' BYE'):
                    counts['bye'] += 1
        worker(gb)

        def media():
            last = {}
            with ps_file.open('wb') as out, ps_file2.open('wb') as out2:
                outputs = {123456789: out, 123456790: out2}
                while not done.is_set():
                    try:
                        b, _ = rtp.recvfrom(65535)
                    except socket.timeout:
                        continue
                    assert b[0] == 0x80 and b[1] & 127 == 96
                    seq = int.from_bytes(b[2:4], 'big')
                    ssrc = int.from_bytes(b[8:12], 'big')
                    assert ssrc in outputs
                    if ssrc in last:
                        assert seq == (last[ssrc] + 1) & 65535
                    last[ssrc] = seq
                    counts['rtp'] += 1
                    outputs[ssrc].write(b[12:])
        worker(media)

        config = tmp / 'configInfo.ini'
        write_ini(cfg, config)
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', INTEGRATION_CHECK='integration-check',
                   INTEGRATION_CAMERA='camera-test', INTEGRATION_GB='gb-test')
        log = tmp / 'app.log'
        with log.open('w') as output:
            proc = subprocess.Popen([binary, '--config', str(config), '--quit-after', '13500'],
                                    env=env, stdout=output, stderr=output)
            began = time.monotonic()
            try:
                if args.media_binary:
                    media = subprocess.run([args.media_binary, f'rtsp://127.0.0.1:{rtsp.port}/test'],
                                           env=env, capture_output=True, text=True, timeout=12)
                    assert media.returncode == 0, media.stdout + media.stderr
                    print('PASS: Qt video probe decoded local RTSP H.264 frames')
                remaining = 10 - (time.monotonic() - began)
                if remaining > 0:
                    time.sleep(remaining)
                sip_request('BYE', channel, 3, 'video-call-0')
                sip_request('BYE', channel2, 5, 'video-call-1')
                assert proc.wait(timeout=12) == 0, log.read_text()
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=8)
                done.set()
                httpd.shutdown()
        for t in threads:
            t.join(timeout=2)
        rtsp.close()
        assert errors.empty(), list(errors.queue)
        for i in range(8):
            segments = sorted((recording_dir / 'MyOverFlow' / f'camera{i+1}').glob('mo_*.mkv'))
            assert len(segments) >= 2, (i, segments, log.read_text())
            recording_probe = subprocess.run(['ffprobe', '-v', 'error', '-show_entries',
                'stream=codec_name,width,height', '-of', 'json', str(segments[0])],
                capture_output=True, text=True, check=True)
            stream = json.loads(recording_probe.stdout)['streams'][0]
            assert stream['codec_name'] == 'h264' and stream['width'] == 160, stream

        assert all(n > 15 for n in counts['serial']), counts
        for key, minimum in [('auth', 2), ('mqtt', 2), ('topo', 2), ('datas', 2), ('ptz', 1),
                             ('register', 2), ('catalog', 1), ('rtp', 10), ('bye', 2)]:
            assert counts[key] >= minimum, (key, counts, log.read_text())
        db = sqlite3.connect(tmp / 'data/alarms.sqlite')
        for i in range(8):
            kinds = {r[0] for r in db.execute('select kind from alarms where sensor=?', (f't{i}',))}
            assert {'alarm', 'recovery'}.issubset(kinds), (i, kinds)
        if args.stall_one_bus:
            kinds = {r[0] for r in db.execute('select kind from alarms where sensor=?', ('t0',))}
            assert 'communication' in kinds, kinds
            assert all(n > 30 for n in counts['serial'][1:]), counts
        db.close()
        for received in [ps_file, ps_file2]:
            probe = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=codec_name,width,height,nb_read_frames', '-count_frames',
                                    '-of', 'json', str(received)], capture_output=True, text=True, check=True)
            streams = json.loads(probe.stdout)['streams']
            assert any(s.get('codec_name') == 'h264' and s.get('width') == 160 and s.get('height') == 120 and int(s.get('nb_read_frames', 0)) > 5 for s in streams), probe.stdout
        print('PASS: 8 RTU buses (noise/bad CRC/fragmentation/retry), SQLite alarm/recovery,')
        print(f'HTTP auth, actual libmosquitto QoS{args.qos} + reconnect/topology, ISAPI alarm preset, SIP Digest/catalog/')
        print('two-channel INVITE/ACK/BYE, PS/RTP -> ffprobe H.264 160x120, 8 RTSP camera recordings. Counts:', counts)
        for fd in masters + slaves:
            os.close(fd)
        mqtt_sock.close()
        sip.close()
        rtp.close()
        httpd.server_close()


if __name__ == '__main__':
    main()
