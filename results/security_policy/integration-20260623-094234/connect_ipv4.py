import socket
import sys

port = int(sys.argv[1])
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.settimeout(1.0)
client.connect(("127.0.0.1", port))
client.close()
