#!/usr/bin/env python3
"""Host-side reference server for testing MP157 before the i.MX6ULL app is built."""

from __future__ import annotations

import argparse
import asyncio
import json


async def handle(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    peer = writer.get_extra_info("peername")
    print("connected", peer)
    try:
        while line := await reader.readline():
            try:
                message = json.loads(line)
                print("received", message)
                response = {"type": "ack", "seq": int(message.get("seq", 0)), "ok": True}
            except (ValueError, TypeError, json.JSONDecodeError) as exc:
                response = {"type": "ack", "seq": 0, "ok": False, "error": str(exc)}
            writer.write((json.dumps(response) + "\n").encode())
            await writer.drain()
    finally:
        writer.close()
        await writer.wait_closed()
        print("disconnected", peer)


async def run(host: str, port: int) -> None:
    server = await asyncio.start_server(handle, host, port)
    print(f"listening on {host}:{port}")
    async with server:
        await server.serve_forever()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9000)
    args = parser.parse_args()
    asyncio.run(run(args.host, args.port))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

