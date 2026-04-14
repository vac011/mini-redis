#!/usr/bin/env python3
"""
ttl_test.py — 验证 SET ... EX 过期功能
"""
import socket, time, sys

HOST, PORT = "127.0.0.1", 6399

def cmd(*args):
    msg = f"*{len(args)}\r\n"
    for a in args:
        a = str(a)
        msg += f"${len(a)}\r\n{a}\r\n"
    with socket.create_connection((HOST, PORT), timeout=3) as s:
        s.sendall(msg.encode())
        time.sleep(0.05)
        return s.recv(4096).decode(errors="replace").strip()

passed = failed = 0

def check(label, got, expected):
    global passed, failed
    ok = expected in got
    print(f"[{'PASS' if ok else 'FAIL'}] {label:<40} got: {got!r}")
    if ok: passed += 1
    else:  failed += 1

# SET 带 EX 1 (1秒后过期)
check("SET mykey myval EX 1", cmd("SET","mykey","myval","EX","1"), "+OK")
check("GET mykey (before expire)",  cmd("GET","mykey"), "$5")

print("  waiting 1.5s for key to expire...")
time.sleep(1.5)

check("GET mykey (after expire)", cmd("GET","mykey"), "$-1")

# 无过期的 key 不受影响
cmd("SET","perm","forever")
time.sleep(0.2)
check("GET perm (no ttl)", cmd("GET","perm"), "forever")

print(f"\n{passed}/{passed+failed} passed")
sys.exit(0 if failed == 0 else 1)
