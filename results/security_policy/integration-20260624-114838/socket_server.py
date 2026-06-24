import socket
import sys
import time

port = int(sys.argv[1])
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("127.0.0.1", port))
server.listen(32)
server.settimeout(0.5)
deadline = time.time() + 90
while time.time() < deadline:
    try:
        conn, _ = server.accept()
        conn.close()
    except socket.timeout:
        pass
server.close()
