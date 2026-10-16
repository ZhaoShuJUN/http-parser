#!/usr/bin/env python3
"""Integration: PTY MCU + HTTP auth + MQTT 3.1.1 fake broker; stdlib-only."""
import http.server
import argparse
import json
import os
import pty
import queue
import signal
import socketserver
import struct
import subprocess
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECEIVED = queue.Queue()
EVENTS = queue.Queue()


def crc16(data):
    c = 0xffff
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c >> 1) ^ 0xa001) if (c & 1) else (c >> 1)
    return c


def serial_frame(t, data):
    blob = bytes([t]) + len(data).to_bytes(2, 'big') + data
    return b'\x7e\x7e' + blob + crc16(blob).to_bytes(2, 'little')


def recv_n(sock, n):
    buf = b''
    while len(buf) < n:
        more = sock.recv(n - len(buf))
        if not more:
            raise ConnectionError('eof')
        buf += more
    return buf


def mqtt_packet(sock):
    typ = recv_n(sock, 1)[0]
    n, mult = 0, 1
    for _ in range(4):
        b = recv_n(sock, 1)[0]
        n += (b & 127) * mult
        if not b & 128:
            break
        mult *= 128
    else:
        raise ValueError('bad MQTT remaining length')
    return typ, recv_n(sock, n)


class Broker(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class MQTT(socketserver.BaseRequestHandler):
    def handle(self):
        sock = self.request
        self.server.clients.put(sock)
        sock.settimeout(10)
        try:
            ty, payload = mqtt_packet(sock)
            assert ty == 0x10, ('bad CONNECT', ty)
            sock.sendall(b'\x20\x02\x00\x00')
            EVENTS.put('connect')
            while True:
                ty, data = mqtt_packet(sock)
                if ty == 0xc0:
                    sock.sendall(b'\xd0\x00')
                    EVENTS.put('ping')
                elif ty in (0x30, 0x32):
                    tlen = int.from_bytes(data[:2], 'big')
                    topic = data[2:2+tlen].decode()
                    pos = 2 + tlen
                    mid = None
                    if ty & 0x06:
                        mid = int.from_bytes(data[pos:pos+2], 'big')
                        pos += 2
                    msg = json.loads(data[pos:])
                    RECEIVED.put((topic, msg))
                    if mid is not None:
                        sock.sendall(b'\x40\x02' + mid.to_bytes(2, 'big'))
                else:
                    EVENTS.put(('unknown', ty))
        except (OSError, ConnectionError):
            return


class Auth(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def do_POST(self):
        assert self.path == '/iot/devices/auth/gateway'
        length = int(self.headers['Content-Length'])
        req = json.loads(self.rfile.read(length))
        assert req['gatewaySN'] == 'YSHY-TEST-GATEWAY-001'
        reply = json.dumps({'gatewayId': 'gw-test-001',
                            'services': [{'protocol': 'mqtt', 'addr': f'127.0.0.1:{self.server.mqttport}'}]}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)
        self.close_connection = True

    def log_message(self, *a):
        pass


class AuthServer(http.server.ThreadingHTTPServer):
    daemon_threads = True


def wait_msg(topic_suffix, timeout=10):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        topic, payload = RECEIVED.get(timeout=max(0.01, until - time.monotonic()))
        if topic.endswith(topic_suffix):
            return topic, payload
    raise AssertionError(f'No MQTT {topic_suffix}')


def run(exe):
    mqtt = Broker(('127.0.0.1', 0), MQTT)
    mqtt.clients = queue.Queue()
    auth = AuthServer(('127.0.0.1', 0), Auth)
    auth.mqttport = mqtt.server_address[1]
    for srv in (mqtt, auth):
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)
    proc = subprocess.Popen([str((ROOT / exe).resolve()), slave_name, '115200', '127.0.0.1', str(auth.server_address[1])],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        # MCU answers the startup configuration query; PTY master captures the request.
        import select
        rf, _, _ = select.select([master], [], [], 8)
        assert rf, 'gateway did not request CFG'
        request = os.read(master, 512)
        assert b'\x7e\x7e\x85' in request, repr(request)
        # Malformed CRC must not create a cloud message or damage parser synchronization.
        bad = bytearray(serial_frame(0x01, b'{"localId":"th_9","data":{"wrong":1}}'))
        bad[-1] ^= 0xf0
        os.write(master, bytes(bad))
        cfg = serial_frame(0x05, b'{"th":2,"door":1,"pm":1,"bat":1}')
        os.write(master, cfg[:3]);time.sleep(.15);os.write(master, cfg[3:])
        top_topic, topo = wait_msg('/topo/add')
        assert top_topic == '/v1/devices/gw-test-001/topo/add'
        assert len(topo['deviceInfo']) == 5, topo
        assert any(x['nodeId'] == '1511INH000100001YSHY-TEST-GATEWAY-001' for x in topo['deviceInfo'])
        assert any(x['nodeId'] == 'pm_1' for x in topo['deviceInfo'])
        data = json.dumps({'serviceId':'analog','devices':[{'localId':'th_1','data':{'temperature':24.8}},
                          {'localId':'th_2','data':{'temperature':25.2}}]},separators=(',',':')).encode()
        os.write(master, serial_frame(0x01, data))
        data_topic, obj = wait_msg('/datas')
        assert data_topic == '/v1/devices/gw-test-001/datas'
        assert len(obj['devices']) == 2, obj
        assert obj['devices'][0]['deviceId'] == 'INH000100001'
        assert obj['devices'][0]['data'] == [{'temperature': 24.8}]
        # Prove default service for discrete type.
        os.write(master, serial_frame(0x03, b'{ "localId" : "door_1", "data" : { "open": true }}'))
        _, obj = wait_msg('/datas')
        assert obj['devices'][0]['serviceId'] == 'discrete', obj
        # Disconnected serial side / malformed very large length should not crash.
        os.write(master, b'\x7e\x7e\x01\xff\xff\x00\x00\x00\x00')
        os.write(master, serial_frame(0x02, b''))
        os.write(master, serial_frame(0x04, b''))
        # Reconnect request following CONFIG CHANGED.
        until = time.monotonic()+8
        seen_cfg=False
        while time.monotonic()<until:
            rr,_,_=select.select([master],[],[],1)
            if rr and b'\x7e\x7e\x85' in os.read(master,512):seen_cfg=True;break
        assert seen_cfg, 'CFG_CHG was not handled'
        # Close the live MQTT socket from the broker side; reconnect must republish topo.
        client = mqtt.clients.get(timeout=4)
        client.shutdown(2)
        client.close()
        # Data received just after disconnect is retained in the RAM FIFO.
        os.write(master, serial_frame(0x01, b'{"localId":"th_1","data":{"queued_after_disconnect":99}}'))
        until = time.monotonic()+8
        while time.monotonic()<until:
            try:
                item=EVENTS.get(timeout=max(.01,until-time.monotonic()))
            except queue.Empty:
                break
            if item=='connect':
                # The first connect event may still be in the queue.
                pass
            if mqtt.clients.qsize():
                break
        assert mqtt.clients.qsize(), 'MQTT did not reconnect'
        top_topic,topo=wait_msg('/topo/add', 8)
        assert len(topo['deviceInfo']) == 5, topo
        _, queued = wait_msg('/datas', 8)
        assert queued['devices'][0]['data'][0]['queued_after_disconnect'] == 99, queued
        # Post-reconnect data and dynamic device discovery (not in CFG_RSP).
        os.write(master, serial_frame(0x01,b'{"serviceId":"analog","localId":"fan_1","data":{"rpm":1200}}'))
        t,topo=wait_msg('/topo/add',8)
        assert any(x['name']=='INH000700001' for x in topo['deviceInfo'])
        t,reading=wait_msg('/datas',8)
        assert reading['devices'][0]['data'][0]['rpm']==1200,reading
        # Test JSON chunking for 30 nodes, which exceeds a single payload.
        os.write(master, serial_frame(0x05, b'{"th":30}'))
        counted = 0
        while counted < 30:
            _, part = wait_msg('/topo/add', 8)
            counted += len(part['deviceInfo'])
        assert counted == 30, f'topology count {counted}'
        if os.getenv('GW_TEST_PING'):
            # Compile test build using -DPING_AFTER_MS=1200ULL to avoid a 30s test.
            deadline=time.monotonic()+8
            got_ping=False
            while time.monotonic()<deadline:
                try:
                    if EVENTS.get(timeout=max(.01,deadline-time.monotonic()))=='ping':
                        got_ping=True
                        break
                except queue.Empty:
                    break
            assert got_ping, 'MQTT PINGREQ or PINGRESP missing'
        assert proc.poll() is None, 'gateway exited early'
        print('PASS: HTTP auth, MQTT, topology, batch, discrete, CRC, split frames, length reject, CFG_CHG, reconnect, live topology updates' + (', PINGREQ/PINGRESP' if os.getenv('GW_TEST_PING') else ''))
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            out,err=proc.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill();out,err=proc.communicate()
        print(err.decode(errors='replace')[-2800:])
        os.close(master);os.close(slave)
        auth.shutdown();mqtt.shutdown();auth.server_close();mqtt.server_close()
        assert proc.returncode == 0, f'gateway failed: {proc.returncode}'


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', default='iot_gateway')
    args = parser.parse_args()
    run(args.exe)
