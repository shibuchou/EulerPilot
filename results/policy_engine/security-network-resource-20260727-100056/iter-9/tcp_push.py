import socket, sys, time
host = sys.argv[1]
port = int(sys.argv[2])
duration = float(sys.argv[3])
payload = b'x' * 65536
sent = 0
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5.0)
sock.connect((host, port))
end = time.time() + duration
while time.time() < end:
    try:
        sent += sock.send(payload)
    except (BlockingIOError, InterruptedError):
        continue
sock.shutdown(socket.SHUT_WR)
sock.close()
mbit = sent * 8.0 / duration / 1000000.0
print(f"mbit_per_sec={mbit:.3f}")
