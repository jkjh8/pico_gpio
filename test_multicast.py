#!/usr/bin/env python3
"""
Multicast UDP Test Client for Pico GPIO Controller

Multicast Group: 239.224.0.1
Command Port: 9000 (TX - send commands here)
Feedback Port: 9001 (RX - receive feedback here)
"""

import socket
import struct
import sys
import threading
import time

MCAST_GROUP = '239.224.0.1'
MCAST_PORT = 9000  # 송수신 공통 포트

def listen_feedback():
    """멀티캐스트 피드백 수신 스레드"""
    print(f"[FEEDBACK] Starting listener on {MCAST_GROUP}:{MCAST_PORT}")
    
    try:
        # UDP 소켓 생성
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        # 모든 인터페이스에서 수신하도록 바인딩
        sock.bind(('', MCAST_PORT))
        print(f"[FEEDBACK] Socket bound to port {MCAST_PORT}")
        
        # 멀티캐스트 그룹 가입
        mreq = struct.pack('4sl', socket.inet_aton(MCAST_GROUP), socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        sock.settimeout(1.0)
        
        print(f"[FEEDBACK] Joined multicast group {MCAST_GROUP}")
        print(f"[FEEDBACK] Listening for feedback on {MCAST_GROUP}:{MCAST_PORT}...")
        
        while True:
            try:
                data, addr = sock.recvfrom(1024)
                if data:
                    msg = data.decode('utf-8', errors='ignore').strip()
                    print(f"\n[<<<] {msg} (from {addr[0]}:{addr[1]})")
                    print("Command> ", end='', flush=True)
            except socket.timeout:
                continue
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"[FEEDBACK] Error: {e}")
    except Exception as e:
        print(f"[FEEDBACK] Fatal error: {e}")
        import traceback
        traceback.print_exc()

def send_command(cmd):
    """멀티캐스트로 명령 전송"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
        
        # 멀티캐스트 인터페이스 설정 (로컬 네트워크)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton('0.0.0.0'))
        
        # 명령에 줄바꿈 추가
        if not cmd.endswith('\n'):
            cmd += '\n'
        
        # 멀티캐스트 그룹으로 전송
        bytes_sent = sock.sendto(cmd.encode('utf-8'), (MCAST_GROUP, MCAST_PORT))
        print(f"[>>>] {cmd.strip()} ({bytes_sent} bytes sent to {MCAST_GROUP}:{MCAST_PORT})")
        sock.close()
    except Exception as e:
        print(f"[ERROR] Failed to send command: {e}")

def main():
    print("="*60)
    print(" Pico GPIO Multicast Test Client")
    print("="*60)
    print(f" Multicast Group: {MCAST_GROUP}")
    print(f" Port (TX/RX)   : {MCAST_PORT}")
    print("="*60)
    print("\nCommands:")
    print("  getout        - Get output states")
    print("  getin         - Get input states")
    print("  set,ch,val    - Set output (ch: 1-16, val: 0-1)")
    print("  out,val       - Set all outputs (val: 0-65535)")
    print("  test          - Run test sequence")
    print("  quit          - Exit\n")
    
    # 피드백 수신 스레드 시작
    feedback_thread = threading.Thread(target=listen_feedback, daemon=True)
    feedback_thread.start()
    
    # 연결 확인을 위해 1초 대기
    time.sleep(1)
    
    # 초기 상태 확인
    print("\n--- Initial Check ---")
    send_command("getout")
    time.sleep(1)
    
    # 대화형 명령 입력
    print("\n--- Interactive Mode ---")
    try:
        while True:
            cmd = input("Command> ").strip()
            if not cmd:
                continue
            if cmd.lower() == 'quit':
                break
            
            # 테스트 시퀀스
            if cmd.lower() == 'test':
                print("\n[TEST] Running test sequence...")
                commands = [
                    ("getout", "Get initial state"),
                    ("set,1,1", "Turn ON channel 1"),
                    ("getout", "Check state"),
                    ("set,1,0", "Turn OFF channel 1"),
                    ("getout", "Check state"),
                ]
                for test_cmd, desc in commands:
                    print(f"  {desc}: {test_cmd}")
                    send_command(test_cmd)
                    time.sleep(0.5)
                print("[TEST] Completed\n")
                continue
            
            send_command(cmd)
            time.sleep(0.1)
    
    except KeyboardInterrupt:
        print("\n\nExiting...")
    
    print("\nBye!")

if __name__ == '__main__':
    main()
