"""Python reference implementation of the F103 <-> MP157 wire protocol."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Iterable, Optional

SOF = b"\xAA\x55"
VERSION = 1
MAX_PAYLOAD = 64
TYPE_SENSOR = 0x01
TYPE_HEARTBEAT = 0x02
TYPE_COMMAND = 0x10
TYPE_COMMAND_ACK = 0x11

SENSOR_VALID_TEMPERATURE_HUMIDITY = 0x01
SENSOR_VALID_LIGHT = 0x02
SENSOR_VALID_ANALOG = 0x04
SENSOR_DATA_SYNTHETIC = 0x80


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def build_frame(msg_type: int, sequence: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    body = struct.pack("<BBHH", VERSION, msg_type, sequence & 0xFFFF, len(payload)) + payload
    return SOF + body + struct.pack("<H", crc16_modbus(body))


@dataclass(frozen=True)
class Message:
    msg_type: int
    sequence: int
    payload: bytes


@dataclass(frozen=True)
class SensorSample:
    temperature_centi_c: int
    humidity_centi_percent: int
    light_raw: int
    analog_raw: int
    valid_flags: int = (
        SENSOR_VALID_TEMPERATURE_HUMIDITY
        | SENSOR_VALID_LIGHT
        | SENSOR_VALID_ANALOG
    )
    alarm: int = 0

    def encode(self) -> bytes:
        return struct.pack(
            "<hHHHBB",
            self.temperature_centi_c,
            self.humidity_centi_percent,
            self.light_raw,
            self.analog_raw,
            self.valid_flags,
            self.alarm,
        )

    @classmethod
    def decode(cls, payload: bytes) -> "SensorSample":
        if len(payload) != 10:
            raise ValueError("sensor payload must be 10 bytes")
        return cls(*struct.unpack("<hHHHBB", payload))


class StreamParser:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes | bytearray | Iterable[int]) -> list[Message]:
        self.buffer.extend(data)
        messages: list[Message] = []
        while True:
            pos = self.buffer.find(SOF)
            if pos < 0:
                if self.buffer[-1:] == SOF[:1]:
                    self.buffer[:] = SOF[:1]
                else:
                    self.buffer.clear()
                break
            if pos:
                del self.buffer[:pos]
            if len(self.buffer) < 8:
                break
            version, msg_type, sequence, payload_len = struct.unpack_from("<BBHH", self.buffer, 2)
            if version != VERSION or payload_len > MAX_PAYLOAD:
                del self.buffer[0]
                continue
            frame_len = 10 + payload_len
            if len(self.buffer) < frame_len:
                break
            frame = bytes(self.buffer[:frame_len])
            expected = struct.unpack_from("<H", frame, 8 + payload_len)[0]
            actual = crc16_modbus(frame[2 : 8 + payload_len])
            del self.buffer[:frame_len]
            if expected != actual:
                continue
            messages.append(Message(msg_type, sequence, frame[8 : 8 + payload_len]))
        return messages


def parse_one(frame: bytes) -> Optional[Message]:
    messages = StreamParser().feed(frame)
    return messages[0] if len(messages) == 1 else None
