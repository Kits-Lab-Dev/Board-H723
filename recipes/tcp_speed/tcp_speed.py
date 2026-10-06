"""Замер скорости TCP с платой (рецепт tcp_speed).

python tcp_speed.py <адрес платы> [секунд] [up|down ...]

up   — ПК шлёт на порт 5001, плата принимает и выбрасывает
down — плата шлёт с порта 5002, ПК принимает
"""
import socket
import sys
import time

RX_PORT = 5001
TX_PORT = 5002


def up(host, secs):
    s = socket.create_connection((host, RX_PORT), timeout=10)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
    buf = bytes(65536)
    n = 0
    t0 = time.time()
    while time.time() - t0 < secs:
        s.sendall(buf)
        n += len(buf)
    # дождаться, пока плата примет всё и закроет соединение
    s.shutdown(socket.SHUT_WR)
    s.recv(1)
    dt = time.time() - t0
    s.close()
    print(f"PC->board: {n / dt * 8 / 1e6:.1f} Mbit/s ({n} B, {dt:.1f} s)")


def down(host, secs):
    s = socket.create_connection((host, TX_PORT), timeout=10)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    n = 0
    t0 = time.time()
    while time.time() - t0 < secs:
        d = s.recv(65536)
        if not d:
            break
        n += len(d)
    dt = time.time() - t0
    s.close()
    print(f"board->PC: {n / dt * 8 / 1e6:.1f} Mbit/s ({n} B, {dt:.1f} s)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    host = sys.argv[1]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10
    for what in sys.argv[3:] or ["up", "down"]:
        {"up": up, "down": down}[what](host, secs)
        time.sleep(1)
