#!/usr/bin/env python3
"""
simple_test.py — mini-redis 功能冒烟测试
直接用 socket 发送 RESP 协议，验证各命令返回值
"""
import socket, time, sys

HOST, PORT = "127.0.0.1", 6399

def send_cmd(sock, *args):
    """构造 RESP 数组并发送，返回原始响应字符串"""
    msg = f"*{len(args)}\r\n"
    for a in args:
        a = str(a)
        msg += f"${len(a)}\r\n{a}\r\n"
    sock.sendall(msg.encode())
    time.sleep(0.05)
    return sock.recv(4096).decode(errors="replace")

def cmd(*args):
    """每条命令新建一个连接（模拟 redis-cli 短连接行为）"""
    with socket.create_connection((HOST, PORT), timeout=2) as s:
        return send_cmd(s, *args).strip()

cases = [
    (["PING"],                          "+PONG"),
    (["SET", "hello", "world"],         "+OK"),
    (["GET", "hello"],                  "$5\r\nworld"),
    (["SET", "counter", "10"],          "+OK"),
    (["INCR", "counter"],               ":11"),
    (["INCR", "counter"],               ":12"),
    (["GET", "counter"],                "$2\r\n12"),
    (["DEL", "hello", "counter"],       ":2"),
    (["EXISTS", "hello"],               ":0"),
    (["GET", "nonexistent"],            "$-1"),
]

passed = failed = 0
for args, expected in cases:
    resp = cmd(*args)
    ok = expected in resp
    status = "PASS" if ok else "FAIL"
    if ok:
        passed += 1
    else:
        failed += 1
    print(f"[{status}] {' '.join(args)!s:<30}  got: {resp!r}")

print(f"\n{passed}/{passed+failed} passed")
sys.exit(0 if failed == 0 else 1)
