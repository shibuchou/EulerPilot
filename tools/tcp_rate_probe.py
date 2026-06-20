#!/usr/bin/env python3
import argparse
import json
import socket
import time
from pathlib import Path


def write_json(path, payload):
    if not path:
        return
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")


def now_ns():
    return time.monotonic_ns()


def mbps(byte_count, duration_s):
    if duration_s <= 0:
        return 0.0
    return byte_count * 8.0 / duration_s / 1_000_000.0


def run_server(args):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    sock.listen(1)
    sock.settimeout(args.max_duration_s)

    if args.ready_file:
        ready = Path(args.ready_file)
        ready.parent.mkdir(parents=True, exist_ok=True)
        ready.write_text("ready\n")

    accepted = False
    byte_count = 0
    start_ns = now_ns()
    end_ns = start_ns
    peer = None
    try:
        conn, peer = sock.accept()
        accepted = True
        with conn:
            conn.settimeout(1.0)
            start_ns = now_ns()
            deadline = time.monotonic() + args.max_duration_s
            while time.monotonic() < deadline:
                try:
                    data = conn.recv(args.chunk_size)
                except socket.timeout:
                    continue
                if not data:
                    break
                byte_count += len(data)
            end_ns = now_ns()
    finally:
        sock.close()

    duration_s = max((end_ns - start_ns) / 1_000_000_000.0, 0.000001)
    result = {
        "role": "server",
        "accepted": accepted,
        "bind": args.bind,
        "port": args.port,
        "peer": str(peer) if peer else "",
        "bytes": byte_count,
        "duration_s": duration_s,
        "mbps": mbps(byte_count, duration_s),
    }
    write_json(args.json_output, result)
    print(json.dumps(result, sort_keys=True))
    return 0 if accepted else 1


def run_client(args):
    payload = b"x" * args.chunk_size
    byte_count = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.connect((args.host, args.port))
    start_ns = now_ns()
    deadline = time.monotonic() + args.duration_s
    try:
        while time.monotonic() < deadline:
            sent = sock.send(payload)
            if sent <= 0:
                break
            byte_count += sent
    finally:
        try:
            sock.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        sock.close()
    end_ns = now_ns()

    duration_s = max((end_ns - start_ns) / 1_000_000_000.0, 0.000001)
    result = {
        "role": "client",
        "host": args.host,
        "port": args.port,
        "bytes": byte_count,
        "duration_s": duration_s,
        "mbps": mbps(byte_count, duration_s),
    }
    write_json(args.json_output, result)
    print(json.dumps(result, sort_keys=True))
    return 0


def main():
    parser = argparse.ArgumentParser(description="Minimal TCP throughput probe for lab veth tests")
    sub = parser.add_subparsers(dest="mode", required=True)

    server = sub.add_parser("server")
    server.add_argument("--bind", required=True)
    server.add_argument("--port", type=int, required=True)
    server.add_argument("--max-duration-s", type=float, default=15.0)
    server.add_argument("--chunk-size", type=int, default=65536)
    server.add_argument("--ready-file", default="")
    server.add_argument("--json-output", default="")

    client = sub.add_parser("client")
    client.add_argument("--host", required=True)
    client.add_argument("--port", type=int, required=True)
    client.add_argument("--duration-s", type=float, default=6.0)
    client.add_argument("--chunk-size", type=int, default=65536)
    client.add_argument("--json-output", default="")

    args = parser.parse_args()
    if args.mode == "server":
        return run_server(args)
    return run_client(args)


if __name__ == "__main__":
    raise SystemExit(main())
