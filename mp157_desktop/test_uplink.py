#!/usr/bin/env python3
"""Real Qt processes + PTY + TCP: synthetic input, never a hardware claim."""
import json
import os
from pathlib import Path
import pty
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request


def until(check, label, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = check()
            if result:
                return result
        except (OSError, sqlite3.Error):
            pass
        time.sleep(.05)
    raise AssertionError(label)


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def frame(seq, flags=15, legacy=False):
    p = (struct.pack('<hHHHBB', 2550, 6100, 1234, 2345, flags, 0) if legacy else
         struct.pack('<hHHHHBB', 2550, 6100, 2048, 1650, 356, flags, 1))
    body = struct.pack('<BBHH', 1, 1, seq, len(p)) + p
    crc = 65535
    for byte in body:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return b'\xaa\x55' + body + struct.pack('<H', crc)


def sql(path, statement):
    with sqlite3.connect(path, timeout=.5) as db:
        return db.execute(statement).fetchall()


def stop(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def serial_ready(proc, name):
    assert proc.poll() is None, 'desktop exited before serial opened'
    for fd in Path(f'/proc/{proc.pid}/fd').iterdir():
        try:
            if os.readlink(fd) == name:
                return True
        except OSError:
            pass
    return False


with tempfile.TemporaryDirectory(prefix='edge-uplink-test-') as tmp:
    root = Path(tmp)
    db, aggdb = root/'desktop.db', root/'aggregator.db'
    # An actual pre-ZE schema, containing an old sample, must migrate intact.
    with sqlite3.connect(aggdb) as con:
        con.execute('CREATE TABLE samples(id INTEGER PRIMARY KEY AUTOINCREMENT,received_at INTEGER NOT NULL,'
                    'gateway TEXT NOT NULL,seq INTEGER,source_seq INTEGER,temperature REAL,humidity REAL,'
                    'light INTEGER,analog INTEGER,alarm INTEGER,peer TEXT,raw_json TEXT)')
        con.execute("INSERT INTO samples(received_at,gateway,seq,light) VALUES(1,'old',1,777)")
    master, slave = pty.openpty()
    serial_name = os.ttyname(slave)
    # QSerialPort uses TIOCEXCL. Holding an extra slave descriptor in this
    # harness would keep exclusivity alive even after killing the application.
    os.close(slave)
    tcp, http, proxy = port(), port(), port()
    cfg = root/'desktop.ini'
    cfg.write_text(f'[serial]\nport={serial_name}\n[storage]\ndatabase={db}\n'
                   f'[uplink]\nhost=127.0.0.1\nport={proxy}\nenabled=true\n')
    acfg = root/'aggregator.ini'
    acfg.write_text(f'[server]\nlisten=127.0.0.1\nport={tcp}\n[http]\nport={http}\n'
                    f'[storage]\ndatabase={aggdb}\n')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', EDGE_DESKTOP_CONFIG=str(cfg))
    env.pop('EDGE_CAPTURE', None)
    procs, streams = [], []

    def launch(args):
        log = open(root/f'process-{len(procs)}.log', 'wb')
        streams.append(log)
        proc = subprocess.Popen(args, env=env, stdout=log, stderr=log)
        procs.append(proc)
        return proc

    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    done = threading.Event()
    ack_dropped, reject_sent = threading.Event(), threading.Event()
    relay_errors = []

    def relay():
        dropped = False
        rejected = False
        while not done.is_set():
            try:
                client, _ = listener.accept()
                with client, socket.create_connection(('127.0.0.1', tcp), timeout=10) as server:
                    client.settimeout(10)
                    reader, replies = client.makefile('rb'), server.makefile('rb')
                    with reader, replies:
                        for line in reader:
                            obj = json.loads(line)
                            # A rejected ACK must also leave the row persistent.
                            if not rejected:
                                rejected = True
                                client.sendall(json.dumps(dict(type='ack', seq=obj['seq'], ok=False,
                                                               error='injected rejection')).encode()+b'\n')
                                reject_sent.set()
                                time.sleep(.4)
                                assert sql(db, 'SELECT COUNT(*) FROM desktop_outbox')[0][0] == 3
                                break
                            server.sendall(line)
                            ack = replies.readline()
                            assert json.loads(ack)['ok'] is True
                            if not dropped:
                                dropped = True
                                ack_dropped.set()
                                break  # Stored at aggregator, ACK lost in transit.
                            # Wrong sequence must not remove a row; valid ACK is fragmented.
                            client.sendall(b'{"type":"ack","seq":999999,"ok":true}\n')
                            time.sleep(.05)
                            client.sendall(ack[:5])
                            time.sleep(.02)
                            client.sendall(ack[5:])
            except (TimeoutError, OSError):
                if done.is_set():
                    return
            except Exception as exc:
                relay_errors.append(repr(exc))
                return

    try:
        desktop = launch([sys.argv[1]])
        until(lambda: db.exists() and sql(db, 'SELECT COUNT(*) FROM desktop_samples') == [(0,)], 'desktop ready')
        until(lambda: serial_ready(desktop, serial_name), 'initial serial open', timeout=45)
        time.sleep(.2)
        os.write(master, frame(65535) + frame(0, 0) + frame(1, 7, True))
        until(lambda: sql(db, 'SELECT COUNT(*) FROM desktop_outbox') == [(3,)], 'offline queue persisted')
        identity = sql(db, 'SELECT gateway FROM desktop_identity')[0][0]
        desktop.kill()
        desktop.wait()
        # Recreate the pseudo device after the crash: a Linux PTY retains its
        # exclusive state while the test owns the master descriptor.
        os.close(master)
        master, slave = pty.openpty()
        serial_name = os.ttyname(slave)
        os.close(slave)
        text = cfg.read_text()
        import re
        cfg.write_text(re.sub(r'(?m)^port=/dev/pts/\d+$', 'port=' + serial_name, text))
        aggregator = launch([sys.argv[2], '--config', str(acfg)])

        def api():
            with urllib.request.urlopen(f'http://127.0.0.1:{http}/api/latest', timeout=1) as response:
                return json.load(response)

        until(api, 'aggregator started')
        listener.bind(('127.0.0.1', proxy))
        listener.listen()
        listener.settimeout(1)
        relay_thread = threading.Thread(target=relay, daemon=True)
        relay_thread.start()
        desktop = launch([sys.argv[1]])
        until(lambda: serial_ready(desktop, serial_name), 'serial open after restart', timeout=45)
        until(lambda: sql(db, 'SELECT COUNT(*) FROM desktop_outbox') == [(0,)], 'replay after process restart')
        assert ack_dropped.is_set() and reject_sent.is_set(), 'fault injection exercised'
        assert not relay_errors, relay_errors
        assert sql(db, 'SELECT gateway FROM desktop_identity')[0][0] == identity
        rows = [r for r in api() if r['gateway'] == identity]
        assert len(rows) == 3, rows
        rows.sort(key=lambda r: r['seq'])
        assert [r['source_seq'] for r in rows] == [65535, 0, 1]
        assert rows[0]['board'] == 'F103ZE'
        assert [rows[0][k] for k in ('ntc', 'millivolts', 'distance', 'digital')] == [2048,1650,356,1]
        assert rows[0]['light'] is None and rows[0]['analog'] is None and rows[0]['alarm'] is None
        assert all(rows[1][k] is None for k in ('temperature','humidity','ntc','millivolts','distance','digital'))
        assert rows[2]['board'] == 'F103C8' and rows[2]['light'] == 1234 and rows[2]['ntc'] is None
        assert sql(aggdb, "SELECT light FROM samples WHERE gateway='old'") == [(777,)]
        # Direct malformed input must be rejected before insertion.
        with socket.create_connection(('127.0.0.1', tcp), timeout=2) as client:
            reader = client.makefile('rb')
            for bad in [dict(type='sensor', gateway='bad', seq=1.5),
                        dict(type='sensor', gateway='bad', seq=1, schema=2, board='F103C8'),
                        dict(type='sensor', gateway='bad', seq=1, schema=2, board='F103ZE', valid_flags=2, ntc=99999, millivolts=1650)]:
                client.sendall(json.dumps(bad).encode()+b'\n')
                assert json.loads(reader.readline())['ok'] is False
            reader.close()
        assert sql(aggdb, "SELECT COUNT(*) FROM samples WHERE gateway='bad'") == [(0,)]
        # Fresh acquisition after restart continues the persistent upload sequence.
        time.sleep(.5)
        os.write(master, frame(1))
        until(lambda: sql(db, 'SELECT COUNT(*) FROM desktop_samples') == [(4,)], 'fresh serial sample after restart')
        until(lambda: sql(aggdb, f"SELECT COUNT(*) FROM samples WHERE gateway='{identity}'") == [(4,)], 'new sample after replay')
        until(lambda: sql(db, 'SELECT COUNT(*) FROM desktop_outbox') == [(0,)], 'final ACK persisted')
        with urllib.request.urlopen(f'http://127.0.0.1:{http}/') as response:
            html = response.read().decode()
            assert '距离 mm' in html and '356' in html and 'NTC mV' in html
        print('PASS: real Qt PTY -> persistent outbox -> TCP -> migrated SQLite -> HTTP')
        print('PASS: offline/restart replay, NACK retention, lost/fragmented/wrong ACK, dedup, source wrap, ZE/C8 isolation, invalid fields')
    except Exception:
        print('desktop rows', sql(db, 'SELECT * FROM desktop_samples'))
        print('outbox', sql(db, 'SELECT * FROM desktop_outbox'))
        print('relay errors', relay_errors)
        for proc in procs:
            stop(proc)
        for stream in streams:
            stream.flush()
        for log in root.glob('*.log'):
            print(log.name, log.read_text(errors='replace'))
        raise
    finally:
        done.set()
        listener.close()
        for proc in procs:
            stop(proc)
        for stream in streams:
            stream.close()
        os.close(master)
