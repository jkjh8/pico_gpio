# 회의실 마이크 컨트롤러

구즈넷 마이크를 회의용 마이크로 변환하는 Pico GPIO 장치의 외부 제어 소프트웨어.
**AMX MU 시리즈**에서 UDP 멀티캐스트로 장치를 제어한다.

---

## 시스템 구성

```
┌─────────────┐   버튼 누름 (멀티캐스트)    ┌──────────────────┐
│  Pico GPIO  │ ─────────────────────────> │                  │
│  장치       │                            │  mic_controller  │
│  (HC165 입력│ <───────────────────────── │  .py             │
│   HC595 출력│   LED 제어 (멀티캐스트)     │                  │
└─────────────┘                            └────────┬─────────┘
                                                    │
                                           멀티캐스트 명령
                                                    │
                                           ┌────────▼─────────┐
                                           │  AMX MU 시리즈   │
                                           │  (외부 제어)     │
                                           └──────────────────┘
```

| 항목 | 내용 |
|------|------|
| 멀티캐스트 그룹 | `239.195.42.17` |
| 포트 | `9000` (송수신 동일) |
| 프로토콜 | UDP, ASCII 텍스트 |
| 채널 수 | 16채널 (1~16) |
| 입력 ch N | Pico 입력 — 마이크 버튼 |
| 출력 ch N | Pico 출력 — 마이크 LED |

---

## 동작 흐름

### 버튼 → LED + DSP

```
마이크 버튼 누름 (Pico HC165 입력)
    │
    │  멀티캐스트: in,0,<ch>,1
    ▼
MicController._on_button_press(ch)
    │
    ├─ 이미 켜져 있으면 → _deactivate()  →  LED OFF + DSP 뮤트
    │
    └─ 꺼져 있으면     → _activate()
                            │
                            ├─ FIFO 한도 초과 시 가장 오래된 마이크 퇴출
                            ├─ LED ON  →  멀티캐스트: set,0,<ch>,1
                            └─ DSP 뮤트 해제  (구현 필요)
```

### FIFO 동시 사용 제한

- `max_mics` 개수를 초과하면 **가장 먼저 켜진** 마이크를 자동으로 끈다.
- **예외 채널**로 등록된 마이크는 퇴출 대상에서 제외된다.
- 활성 마이크가 모두 예외 채널이면 한도 초과를 허용하고 경고 로그를 남긴다.

```
활성: [ch1, ch2, ch3]  max_mics=2  예외={}

ch4 버튼 누름
  → ch1 퇴출 (가장 오래됨)
  → ch4 활성
  → 활성: [ch2, ch3, ch4]
```

```
활성: [ch1, ch2, ch3]  max_mics=2  예외={ch1}

ch4 버튼 누름
  → ch1 건너뜀 (예외)
  → ch2 퇴출 (가장 오래된 비예외)
  → ch4 활성
  → 활성: [ch1, ch3, ch4]
```

---

## 실행

```bash
python mic_controller.py
```

### 설정 변경 (코드 상단 상수)

```python
MCAST_GROUP      = "239.195.42.17"  # 멀티캐스트 그룹 IP
MCAST_PORT       = 9000           # 포트
DEVICE_ID        = 0              # 0 = 전체 장치 브로드캐스트
MAX_MICS_DEFAULT = 2              # 초기 동시 사용 제한
```

---

## 외부 제어 명령 (AMX MU → 멀티캐스트)

모든 명령은 `239.195.42.17:9000` UDP 멀티캐스트로 전송한다.

### 마이크 LED 강제 제어

| 명령 | 설명 | 예시 |
|------|------|------|
| `MIC_ON,<device_id>,<ch>` | 마이크 LED 강제 ON | `MIC_ON,0,3` |
| `MIC_OFF,<device_id>,<ch>` | 마이크 LED 강제 OFF | `MIC_OFF,0,3` |

> `device_id=0` : 네트워크 내 모든 장치에 적용

### 동시 사용 한도 변경 (실시간)

| 명령 | 설명 | 예시 |
|------|------|------|
| `MIC_LIMIT,<n>` | 동시 사용 마이크 수 설정 | `MIC_LIMIT,3` |
| `MIC_LIMIT,0` | 제한 없음 | `MIC_LIMIT,0` |

한도를 줄이면 초과 마이크가 **즉시** FIFO 순서로 퇴출된다.

### 예외 채널 관리

| 명령 | 설명 | 예시 |
|------|------|------|
| `MIC_EXCEPT,<ch>,1` | 채널을 예외로 등록 | `MIC_EXCEPT,1,1` |
| `MIC_EXCEPT,<ch>,0` | 예외 해제 | `MIC_EXCEPT,1,0` |

예외 채널은 한도 초과 시 퇴출 대상에서 제외된다.

### 상태 조회

| 명령 | 설명 |
|------|------|
| `MIC_STATUS` | 현재 상태를 멀티캐스트로 브로드캐스트 |

응답 형식:
```
MIC_STATUS_REPLY,limit=2,active=2,5,exceptions=1
```

---

## Pico 장치 프로토콜 (참고)

컨트롤러가 내부적으로 사용하는 Pico 장치 원시 프로토콜.

### 수신 (Pico → 컨트롤러)

```
in,<device_id>,<channel>,<value>
```

| 예시 | 의미 |
|------|------|
| `in,1,3,1` | 장치 1, 채널 3 버튼 눌림 |
| `in,1,3,0` | 장치 1, 채널 3 버튼 놓임 |

### 송신 (컨트롤러 → Pico)

```
set,<device_id>,<channel>,<value>
```

| 예시 | 의미 |
|------|------|
| `set,0,3,1` | 전체 장치, 채널 3 LED ON |
| `set,0,3,0` | 전체 장치, 채널 3 LED OFF |

---

## DSP 연동

`mic_controller.py` 내 `_dsp_unmute()` / `_dsp_mute()` 메서드에 DSP 명령을 구현한다.

```python
def _dsp_unmute(self, ch: int):
    # TODO: DSP 명령 구현
    pass

def _dsp_mute(self, ch: int):
    # TODO: DSP 명령 구현
    pass
```

### DSP 종류별 구현 예시

**QSC Q-SYS** (TCP, 포트 1702)
```python
self._dsp_sock.sendall(f"set Mic{ch} Mute 0\n".encode())  # 뮤트 해제
self._dsp_sock.sendall(f"set Mic{ch} Mute 1\n".encode())  # 뮤트
```

**Biamp Tesira** (TCP, 포트 44100)
```python
self._dsp_sock.sendall(f"DEVICE set inputMute {ch} false\n".encode())
self._dsp_sock.sendall(f"DEVICE set inputMute {ch} true\n".encode())
```

**AMX NetLinx** (NetLinx 스크립트)
```netlinx
send_string dvDSP, "'MUTE OFF ', itoa(ch)"  // 뮤트 해제
send_string dvDSP, "'MUTE ON  ', itoa(ch)"  // 뮤트
```

**BSS Soundweb London** (UDP, 포트 1023)
```python
# BLU-link 바이너리 프레임 사용
```

---

## 파일 구조

```
mic_controller/
├── mic_controller.py   # 컨트롤러 본체
└── README.md           # 이 문서
```

---

## 요구 사항

- Python 3.9 이상
- 표준 라이브러리만 사용 (`socket`, `threading`, `logging`, `collections`)
- 추가 패키지 설치 불필요
