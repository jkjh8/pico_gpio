#!/usr/bin/env python3
"""
멀티캐스트 수신만 테스트
"""
import socket
import struct

MCAST_GROUP = '239.224.0.1'
FEEDBACK_PORT = 9001

print(f"멀티캐스트 수신 테스트: {MCAST_GROUP}:{FEEDBACK_PORT}")

# UDP 소켓 생성
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

# 포트 바인딩
sock.bind(('', FEEDBACK_PORT))
print(f"포트 {FEEDBACK_PORT} 바인딩 완료")

# 멀티캐스트 그룹 가입
mreq = struct.pack('4sl', socket.inet_aton(MCAST_GROUP), socket.INADDR_ANY)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
print(f"멀티캐스트 그룹 {MCAST_GROUP} 가입 완료")
print("대기 중... (Ctrl+C로 종료)")

try:
    while True:
        data, addr = sock.recvfrom(1024)
        msg = data.decode('utf-8', errors='ignore')
        print(f"[수신] {len(data)}bytes from {addr[0]}:{addr[1]}: {msg.strip()}")
except KeyboardInterrupt:
    print("\n종료")
