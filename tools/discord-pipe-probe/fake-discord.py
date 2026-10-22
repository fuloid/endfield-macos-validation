"""A stand-in for the Discord client's IPC socket, for engine/ci-smoke.sh.

Listens on <dir>/discord-ipc-0, answers HANDSHAKE with READY and every FRAME with an echo of its
cmd and nonce, and appends one JSON line per received frame to <log>:
{"conn": n, "op": op, "payload": {...}}.

    python3 -I fake-discord.py <dir> <log>
"""
import json
import os
import socket
import struct
import sys
import threading

ipc_dir, log_path = sys.argv[1], sys.argv[2]
os.makedirs(ipc_dir, exist_ok=True)
path = os.path.join(ipc_dir, "discord-ipc-0")
if os.path.exists(path):
    os.unlink(path)
lock = threading.Lock()


def recv_exact(conn, size):
    data = b""
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


def send(conn, op, payload):
    body = json.dumps(payload).encode()
    conn.sendall(struct.pack("<II", op, len(body)) + body)


def serve(conn, number):
    try:
        while True:
            op, length = struct.unpack("<II", recv_exact(conn, 8))
            payload = json.loads(recv_exact(conn, length) or b"{}")
            with lock, open(log_path, "a") as log:
                log.write(json.dumps({"conn": number, "op": op, "payload": payload}) + "\n")
            if op == 0:
                send(conn, 1, {"cmd": "DISPATCH", "evt": "READY", "data": {"v": 1, "user": {"id": "1", "username": "ci"}}})
            elif op == 1:
                send(conn, 1, {"cmd": payload.get("cmd"), "nonce": payload.get("nonce"), "data": {}})
    except (EOFError, OSError, ValueError):
        pass
    finally:
        conn.close()


server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(path)
server.listen(8)
count = 0
while True:
    client, _ = server.accept()
    count += 1
    threading.Thread(target=serve, args=(client, count), daemon=True).start()
