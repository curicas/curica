import socket
import os
import sys

sock_path = '/tmp/curica_ipc.sock'
if os.path.exists(sock_path):
    os.remove(sock_path)

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.bind(sock_path)
s.listen(1)

print("Host: Listening on", sock_path)
conn, addr = s.accept()
print("Host: Curica attached!")

conn.sendall(b'{"cmd": "ping"}')
resp = conn.recv(1024)
print("Host: Received from Curica:", resp)
conn.close()
s.close()
