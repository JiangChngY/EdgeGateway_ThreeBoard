#!/usr/bin/env python3
"""Simulate MP157 NDJSON/TCP uplink to the i.MX6ULL aggregator."""

from __future__ import annotations

import argparse
import json
import math
import socket
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--gateway", default="mp157-sim")
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--interval", type=float, default=1.0)
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        stream = sock.makefile("rwb", buffering=0)
        for seq in range(1, args.count + 1):
            payload = {
                "type": "sensor",
                "gateway": args.gateway,
                "seq": seq,
                "timestamp": int(time.time()),
                "source_seq": seq,
                "valid_flags": 0x07,
                "temperature": round(25.0 + math.sin(seq / 4.0) * 2.0, 2),
                "humidity": round(58.0 + math.cos(seq / 5.0) * 4.0, 2),
                "light": 500 + seq * 7,
                "analog": 1000 + seq * 11,
                "alarm": False,
            }
            stream.write((json.dumps(payload, ensure_ascii=False) + "\n").encode())
            ack = stream.readline().decode().strip()
            print("TX", payload)
            print("RX", ack)
            time.sleep(args.interval)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
