#!/usr/bin/env python3
import argparse
import asyncio
import random
import struct
import time
from collections import Counter, defaultdict


def build_frame(msg_type: int, body: bytes) -> bytes:
    length = 2 + len(body)
    return struct.pack("!I", length) + struct.pack("!H", msg_type) + body


async def send_loop(writer: asyncio.StreamWriter, msg_type: int, payload: bytes, rate_per_conn: float, duration: float, stats: dict):
    interval = 1.0 / rate_per_conn if rate_per_conn > 0 else 0
    end_at = time.time() + duration
    while time.time() < end_at:
        try:
            writer.write(build_frame(msg_type, payload))
            await writer.drain()
            stats["sent"] += 1
        except Exception:
            stats["send_errors"] += 1
            return
        if interval > 0:
            await asyncio.sleep(interval)


async def recv_loop(reader: asyncio.StreamReader, stats: dict, error_types: Counter, stop_event: asyncio.Event):
    try:
        while not stop_event.is_set():
            header = await reader.readexactly(4)
            (length,) = struct.unpack("!I", header)
            body = await reader.readexactly(length)
            msg_type = struct.unpack("!H", body[:2])[0]
            payload = body[2:]
            stats["recv"] += 1
            error_types[msg_type] += 1
    except asyncio.IncompleteReadError:
        stats["recv_errors"] += 1
    except Exception:
        stats["recv_errors"] += 1


async def worker(host, port, msg_type, payload, rate_per_conn, duration, stats_global, error_types_global):
    try:
        reader, writer = await asyncio.open_connection(host, port)
    except Exception:
        stats_global["connect_fail"] += 1
        return
    stats = defaultdict(int)
    error_types = Counter()
    stop_event = asyncio.Event()
    recv_task = asyncio.create_task(recv_loop(reader, stats, error_types, stop_event))
    await send_loop(writer, msg_type, payload, rate_per_conn, duration, stats)
    stop_event.set()
    recv_task.cancel()
    writer.close()
    try:
        await writer.wait_closed()
    except Exception:
        pass
    for k, v in stats.items():
        stats_global[k] += v
    for k, v in error_types.items():
        error_types_global[k] += v


async def main():
    parser = argparse.ArgumentParser(description="Simple TCP benchmark for msgType/QPS/backpressure.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--msg-type", type=int, default=2, help="msgType to send")
    parser.add_argument("--payload-size", type=int, default=16, help="payload size in bytes")
    parser.add_argument("--concurrency", type=int, default=10, help="number of parallel connections")
    parser.add_argument("--qps", type=float, default=100.0, help="total QPS")
    parser.add_argument("--duration", type=float, default=10.0, help="seconds to run")
    args = parser.parse_args()

    payload = bytes(random.getrandbits(8) for _ in range(args.payload_size))
    rate_per_conn = args.qps / args.concurrency if args.concurrency > 0 else 0

    stats_global = defaultdict(int)
    error_types_global = Counter()

    tasks = [
        worker(args.host, args.port, args.msg_type, payload, rate_per_conn, args.duration, stats_global, error_types_global)
        for _ in range(args.concurrency)
    ]

    start = time.time()
    await asyncio.gather(*tasks)
    elapsed = time.time() - start

    print(f"Ran {elapsed:.2f}s, concurrency={args.concurrency}, total_qps={args.qps}")
    print(f"Sent={stats_global['sent']} Recv={stats_global['recv']} SendErr={stats_global['send_errors']} RecvErr={stats_global['recv_errors']} ConnFail={stats_global['connect_fail']}")
    if error_types_global:
        print("Response msgType counts:")
        for mt, cnt in error_types_global.most_common():
            print(f"  {mt}: {cnt}")


if __name__ == "__main__":
    asyncio.run(main())
