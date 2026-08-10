#!/usr/bin/env python3
"""Decode F103 binary frames from a USB-TTL serial port (requires pyserial)."""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from edge_protocol import SensorSample, StreamParser, TYPE_SENSOR  # noqa: E402


def main() -> int:
    try:
        import serial
    except ImportError:
        print("pyserial未安装：python -m pip install pyserial", file=sys.stderr)
        return 2

    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="例如 COM5 或 /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    decoder = StreamParser()
    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        print(f"monitoring {args.port} @ {args.baud}")
        while True:
            for message in decoder.feed(port.read(256)):
                if message.msg_type == TYPE_SENSOR:
                    sample = SensorSample.decode(message.payload)
                    print(time.strftime("%H:%M:%S"), f"seq={message.sequence}", sample)
                else:
                    print(time.strftime("%H:%M:%S"), f"type={message.msg_type:#x}",
                          f"seq={message.sequence}", message.payload.hex())


if __name__ == "__main__":
    raise SystemExit(main())

