from __future__ import annotations

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from edge_protocol import (  # noqa: E402
    TYPE_SENSOR,
    SensorSample,
    StreamParser,
    build_frame,
    crc16_modbus,
    parse_one,
)


class ProtocolTests(unittest.TestCase):
    def test_known_crc(self):
        self.assertEqual(crc16_modbus(b"123456789"), 0x4B37)

    def test_sensor_round_trip(self):
        sample = SensorSample(2560, 6123, 789, 2048, 7, 1)
        frame = build_frame(TYPE_SENSOR, 42, sample.encode())
        message = parse_one(frame)
        self.assertIsNotNone(message)
        self.assertEqual(message.sequence, 42)
        self.assertEqual(SensorSample.decode(message.payload), sample)

    def test_stream_fragmentation_and_noise(self):
        frames = [
            build_frame(TYPE_SENSOR, n, SensorSample(2000 + n, 5000, n, 100 + n).encode())
            for n in range(1, 6)
        ]
        stream = b"noise" + b"".join(frames)
        parser = StreamParser()
        output = []
        for start in range(0, len(stream), 3):
            output.extend(parser.feed(stream[start : start + 3]))
        self.assertEqual([m.sequence for m in output], [1, 2, 3, 4, 5])

    def test_bad_crc_is_dropped_and_next_frame_recovers(self):
        bad = bytearray(build_frame(TYPE_SENSOR, 7, SensorSample(1, 2, 3, 4).encode()))
        bad[-1] ^= 0xFF
        good = build_frame(TYPE_SENSOR, 8, SensorSample(5, 6, 7, 8).encode())
        messages = StreamParser().feed(bytes(bad) + good)
        self.assertEqual([m.sequence for m in messages], [8])


if __name__ == "__main__":
    unittest.main(verbosity=2)

