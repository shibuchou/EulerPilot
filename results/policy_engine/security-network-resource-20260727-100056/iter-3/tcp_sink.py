import socket, sys, time
host = sys.argv[1]
port = int(sys.argv[2])
duration = float(sys.argv[3])
end = time.time() + duration + 2.0
received = 0
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((host, port))
srv.listen(1)
srv.settimeout(duration + 5.0)
conn, _ = srv.accept()
conn.settimeout(1.0)
while time.time() < end:
    try:
        data = conn.recv(65536)
    except socket.timeout:
        continue
    if not data:
        break
    received += len(data)
conn.close()
srv.close()
print(received)
