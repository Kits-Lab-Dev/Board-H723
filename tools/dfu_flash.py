#!/usr/bin/env python3
"""Прошивка Доски-H723 по USB без кнопок и без отладчика.

1. Шлёт UDP-пакет "dfu" на 192.168.4.1:7123 (плата в USB-сети) — прошивка
   перезагружается в системный загрузчик STM32 (USB DFU).
2. Ждёт DFU-устройство и пишет файл через STM32_Programmer_CLI.
3. Запускает прошивку и ждёт, пока плата снова ответит на ping.

Нужны Python 3 и STM32_Programmer_CLI в PATH (входит в STM32CubeCLT).
Если плата не отвечает (нет USB-сети, прошивка без команды dfu, плата
зависла) — зажмите BOOT, нажмите RESET или переподключите USB, отпустите
BOOT и запустите скрипт снова: он увидит DFU и прошьёт сразу.

    python tools/dfu_flash.py [файл.hex|.elf|.bin]
"""
import argparse
import os
import platform
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_FILE = os.path.join(ROOT, "examples", "BoardH723", "build", "Release", "BoardH723.hex")
BOARD_IP = "192.168.4.1"
DFU_PORT = 7123
FLASH_ADDR = "0x08000000"


def cli():
    exe = shutil.which("STM32_Programmer_CLI")
    if not exe:
        sys.exit("STM32_Programmer_CLI не найден в PATH — установите STM32CubeCLT")
    return exe


def dfu_present(exe):
    out = subprocess.run([exe, "-l", "usb"], capture_output=True, text=True, errors="replace").stdout
    return "DFU in" in out or "Device Index" in out


def request_dfu():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(2)
    for _ in range(3):
        try:
            s.sendto(b"dfu", (BOARD_IP, DFU_PORT))
            data, _ = s.recvfrom(16)
            if data.startswith(b"ok"):
                return True
        except OSError:
            pass
    return False


def wait(cond, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return True
        time.sleep(1)
    return False


def ping():
    flag = "-n" if platform.system() == "Windows" else "-c"
    r = subprocess.run(["ping", flag, "1", BOARD_IP], capture_output=True, text=True, errors="replace")
    return r.returncode == 0 and "TTL" in r.stdout.upper()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("file", nargs="?", default=DEFAULT_FILE, help="прошивка (.hex, .elf или .bin с адреса 0x08000000)")
    ap.add_argument("--verify", action="store_true", help="сверить после записи (+20 с)")
    args = ap.parse_args()

    if not os.path.isfile(args.file):
        sys.exit(f"нет файла {args.file} — соберите проект: cd examples/BoardH723 && "
                 "cmake --preset Release && cmake --build build/Release")
    exe = cli()

    if dfu_present(exe):
        print("плата уже в DFU")
    else:
        print(f"запрос DFU: UDP {BOARD_IP}:{DFU_PORT}")
        if not request_dfu():
            sys.exit("плата не ответила на запрос DFU. Зажмите BOOT, нажмите RESET "
                     "(или переподключите USB), отпустите BOOT и запустите скрипт снова")
        if not wait(lambda: dfu_present(exe), 15):
            sys.exit("плата ответила, но DFU-устройство не появилось (драйвер DFU установлен?)")
        print("плата в DFU")

    cmd = [exe, "-c", "port=usb1", "-w", args.file]
    if args.file.lower().endswith(".bin"):
        cmd.append(FLASH_ADDR)
    if args.verify:
        cmd.append("-v")
    cmd += ["-s", FLASH_ADDR]
    print(" ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    ok = "File download complete" in r.stdout and "Start operation achieved successfully" in r.stdout
    if not ok:
        print(r.stdout[-3000:])
        sys.exit("ошибка прошивки")
    print("записано, прошивка запущена")

    if wait(ping, 30):
        print(f"плата отвечает: {BOARD_IP}")
    else:
        print(f"плата не ответила на ping {BOARD_IP} за 30 с — смотрите лог USART1 (115200)")
        sys.exit(1)


if __name__ == "__main__":
    main()
