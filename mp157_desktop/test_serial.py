#!/usr/bin/env python3
"""Linux PTY integration test against the real compiled Qt application (no hardware)."""
import json
import os
from pathlib import Path
import pty
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time

def crc(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xa001 if value & 1 else 0)
    return value

def frame(seq, flags=15, legacy=False):
    payload = (struct.pack('<hHHHBB', 2550, 6100, 1234, 2345, flags, 0) if legacy else
               struct.pack('<hHHHHBB', 2550, 6100, 2048, 1650, 356, flags, 1))
    body = struct.pack('<BBHH', 1, 1, seq, len(payload)) + payload
    return b'\xaa\x55' + body + struct.pack('<H', crc(body))

assert crc(b'123456789') == 0x4b37
with tempfile.TemporaryDirectory(prefix='edge-desktop-test-') as tmp:
    tmp = Path(tmp)
    master, slave = pty.openpty()
    db = tmp / 'samples.db'
    config = tmp / 'desktop.ini'
    config.write_text(f'[serial]\nport={os.ttyname(slave)}\nauto_open=true\n[storage]\ndatabase={db}\n')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', EDGE_DESKTOP_CONFIG=str(config))
    env.pop('EDGE_CAPTURE', None)
    proc = subprocess.Popen([sys.argv[1]], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    def rows():
        try:
            with sqlite3.connect(db) as conn:
                return [json.loads(r[0]) for r in conn.execute('SELECT payload FROM desktop_samples ORDER BY id')]
        except sqlite3.OperationalError:
            return []
    def wait_rows(count):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise AssertionError('Qt exited: ' + proc.stderr.read().decode())
            result = rows()
            if len(result) >= count:
                return result
            time.sleep(.05)
        raise AssertionError(f'Expected {count} samples, received {rows()}')
    try:
        time.sleep(1)
        # Noise, split header/payload, then concatenated frames with invalid flags.
        os.write(master, b'noise\xaa' + frame(1)[:3])
        time.sleep(.05)
        os.write(master, frame(1)[3:])
        r = wait_rows(1)[0]
        assert (r['temperature'],r['humidity'],r['ntc'],r['millivolts'],r['distance'],r['digital']) == (25.5,61,2048,1650,356,1)
        broken = bytearray(frame(2)); broken[-1] ^= 1
        os.write(master, broken + frame(3,0) + frame(4,7,True))
        r = wait_rows(3)
        assert [x['sequence'] for x in r] == [1,3,4]
        assert all(r[1][k] is None for k in ('temperature','humidity','ntc','distance','digital'))
        assert r[2]['board'] == 'F103C8' and r[2]['light'] == 1234 and 'distance' not in r[2]
        # Invalid length header must not prevent parsing the next valid frame.
        os.write(master, b'\xaa\x55\x01\x01\x01\x00\xff\xff' + frame(5))
        assert wait_rows(4)[-1]['sequence'] == 5
        print('PASS: CRC vector, PTY reception, fragmentation/noise, bad CRC recovery,')
        print('      null invalid fields, legacy/ZE isolation, bad length recovery, SQLite storage')
    finally:
        proc.terminate()
        try: out, err = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill(); out, err = proc.communicate()
        os.close(master); os.close(slave)
        if err: print(err.decode(), file=sys.stderr)
