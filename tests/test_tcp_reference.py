from __future__ import annotations

import asyncio
import json
import unittest


class TcpReferenceTests(unittest.IsolatedAsyncioTestCase):
    async def test_newline_json_ack(self):
        received = []

        async def handler(reader, writer):
            line = await reader.readline()
            message = json.loads(line)
            received.append(message)
            writer.write((json.dumps({"type": "ack", "seq": message["seq"], "ok": True}) + "\n").encode())
            await writer.drain()
            writer.close()
            await writer.wait_closed()

        server = await asyncio.start_server(handler, "127.0.0.1", 0)
        port = server.sockets[0].getsockname()[1]
        async with server:
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            message = {"type": "sensor", "gateway": "mp157-test", "seq": 99, "temperature": 25.5}
            writer.write((json.dumps(message) + "\n").encode())
            await writer.drain()
            ack = json.loads(await reader.readline())
            writer.close()
            await writer.wait_closed()
        self.assertEqual(received, [message])
        self.assertEqual(ack, {"type": "ack", "seq": 99, "ok": True})


if __name__ == "__main__":
    unittest.main(verbosity=2)

