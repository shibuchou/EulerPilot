import socket
import sys
import time

port = int(sys.argv[1])
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", port))
srv.listen(32)
srv.settimeout(0.5)
deadline = time.time() + 30
while time.time() < deadline:
    try:
        conn, _ = srv.accept()
        conn.close()
    except socket.timeout:
        pass
srv.close()
