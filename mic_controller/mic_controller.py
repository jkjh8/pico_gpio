#!/usr/bin/env python3
"""
Conference Gooseneck Microphone Controller
AMX MU Series External Control via Multicast

Pico Device Protocol (239.224.0.1:9000, UDP multicast):
  RX (device → controller): in,<device_id>,<channel>,<value>
  TX (controller → device): set,<device_id>,<channel>,<value>

External Control Commands (AMX MU → 239.224.0.1:9000):
  MIC_ON,<device_id>,<channel>     - Force turn on mic LED
  MIC_OFF,<device_id>,<channel>    - Force turn off mic LED
  MIC_LIMIT,<count>                - Set max simultaneous mics (0 = unlimited)
  MIC_EXCEPT,<channel>,<1|0>       - Add/remove channel from eviction exception
  MIC_STATUS                       - Broadcast current status

Design:
  - Input ch N  = mic button (HC165 shift register, Pico input)
  - Output ch N = mic LED   (HC595 shift register, Pico output)
  - Button press toggles mic on/off
  - FIFO eviction: when active mics exceed limit, oldest non-exception mic is
    turned off automatically
"""

import socket
import struct
import threading
import time
import logging
from collections import deque
from typing import Set, Optional

# ─── Configuration ────────────────────────────────────────────────────────────

MCAST_GROUP      = "239.224.0.1"
MCAST_PORT       = 9000
MCAST_TTL        = 4
DEVICE_ID        = 0            # 0 = broadcast to all devices on network
MAX_MICS_DEFAULT = 2            # Default max simultaneous active mics
MIC_CHANNELS     = 16          # Total channels (1-based: 1..16)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("mic_controller")


# ─── Controller ───────────────────────────────────────────────────────────────

class MicController:
    """
    Conference microphone controller.

    Responsibilities:
      1. Listen for mic button presses from the Pico device via multicast.
      2. Toggle mic LED and DSP mute/unmute accordingly.
      3. Enforce a configurable maximum simultaneous mic limit using FIFO eviction.
      4. Accept real-time control commands from AMX MU series (or any multicast client).
    """

    def __init__(
        self,
        device_id: int = DEVICE_ID,
        max_mics: int = MAX_MICS_DEFAULT,
    ):
        self.device_id = device_id
        self.max_mics  = max_mics   # 0 = unlimited

        self._lock       = threading.Lock()
        self._active     : deque[int] = deque()   # FIFO, oldest first
        self._exceptions : Set[int]   = set()     # channels exempt from eviction

        self._sock    = self._create_socket()
        self._running = False
        self._thread  = threading.Thread(target=self._receive_loop, daemon=True)

    # ── Socket ────────────────────────────────────────────────────────────────

    def _create_socket(self) -> socket.socket:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", MCAST_PORT))

        # Join multicast group
        mreq = struct.pack("4sL", socket.inet_aton(MCAST_GROUP), socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

        # TTL for outgoing multicast packets
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, MCAST_TTL)
        sock.settimeout(1.0)
        return sock

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    def start(self):
        self._running = True
        self._thread.start()
        log.info(
            "MicController started | device_id=%d  max_mics=%s",
            self.device_id,
            self.max_mics if self.max_mics > 0 else "unlimited",
        )

    def stop(self):
        self._running = False
        self._thread.join(timeout=3)
        self._sock.close()
        log.info("MicController stopped")

    # ── Receive loop ──────────────────────────────────────────────────────────

    def _receive_loop(self):
        while self._running:
            try:
                data, addr = self._sock.recvfrom(1024)
            except socket.timeout:
                continue
            except OSError:
                break

            msg = data.decode("ascii", errors="ignore").strip()
            if msg:
                log.debug("RX %s: %s", addr[0], msg)
                self._dispatch(msg)

    def _dispatch(self, msg: str):
        parts = msg.split(",")
        cmd   = parts[0].upper()

        # ── Pico device input feedback ────────────────────────────────────────
        # Format: in,<device_id>,<channel>,<value>
        if cmd == "IN" and len(parts) == 4:
            try:
                ch    = int(parts[2])
                value = int(parts[3])
            except ValueError:
                return
            if value == 1:
                self._on_button_press(ch)
            # Button release (value=0) is ignored; toggle happens on press only
            return

        # ── External control commands (AMX MU series or any multicast client) ─

        if cmd == "MIC_ON" and len(parts) >= 3:
            try:
                ch = int(parts[2])
            except ValueError:
                return
            self.external_mic_on(ch)

        elif cmd == "MIC_OFF" and len(parts) >= 3:
            try:
                ch = int(parts[2])
            except ValueError:
                return
            self.external_mic_off(ch)

        elif cmd == "MIC_LIMIT" and len(parts) >= 2:
            try:
                limit = int(parts[1])
            except ValueError:
                return
            self.set_max_mics(limit)

        elif cmd == "MIC_EXCEPT" and len(parts) >= 3:
            try:
                ch    = int(parts[1])
                state = int(parts[2])
            except ValueError:
                return
            self.set_exception(ch, bool(state))

        elif cmd == "MIC_STATUS":
            self._broadcast_status()

    # ── Button event ──────────────────────────────────────────────────────────

    def _on_button_press(self, ch: int):
        """Physical mic button pressed → toggle mic on/off."""
        with self._lock:
            if ch in self._active:
                self._deactivate(ch)
                log.info("Mic OFF (button toggle) ch=%d", ch)
            else:
                self._activate(ch)
                log.info("Mic ON  (button press)  ch=%d", ch)

    # ── Core activation logic ─────────────────────────────────────────────────

    def _activate(self, ch: int):
        """
        Turn on mic channel, enforcing FIFO simultaneous-mic limit.
        Must be called with self._lock held.
        """
        if ch in self._active:
            return  # already on

        # Evict oldest non-exception mic(s) while over limit
        if self.max_mics > 0:
            while len(self._active) >= self.max_mics:
                candidate = self._find_evict_candidate()
                if candidate is None:
                    # All active mics are exceptions → cannot evict; allow over-limit
                    log.warning(
                        "All %d active mics are exceptions; limit %d exceeded",
                        len(self._active), self.max_mics,
                    )
                    break
                self._deactivate(candidate)
                log.info("Evicted (FIFO limit=%d) ch=%d", self.max_mics, candidate)

        self._active.append(ch)
        self._set_led(ch, True)
        self._dsp_unmute(ch)

    def _deactivate(self, ch: int):
        """
        Turn off mic channel.
        Must be called with self._lock held.
        """
        if ch not in self._active:
            return
        try:
            self._active.remove(ch)
        except ValueError:
            pass
        self._set_led(ch, False)
        self._dsp_mute(ch)

    def _find_evict_candidate(self) -> Optional[int]:
        """Return oldest active mic not in the exception list, or None."""
        for ch in self._active:   # deque iterates oldest → newest
            if ch not in self._exceptions:
                return ch
        return None

    # ── Device I/O ────────────────────────────────────────────────────────────

    def _send(self, cmd: str):
        if not cmd.endswith("\n"):
            cmd += "\n"
        try:
            self._sock.sendto(cmd.encode("ascii"), (MCAST_GROUP, MCAST_PORT))
            log.debug("TX: %s", cmd.strip())
        except OSError as e:
            log.error("Send error: %s", e)

    def _set_led(self, ch: int, on: bool):
        """Control mic LED via Pico GPIO output channel."""
        self._send(f"set,{self.device_id},{ch},{1 if on else 0}")
        log.debug("LED ch=%d → %s", ch, "ON" if on else "OFF")

    # ── DSP Integration ───────────────────────────────────────────────────────
    #
    # TODO: Implement DSP mute/unmute commands for your specific DSP processor.
    #
    # Common DSP protocols:
    #
    #   QSC Q-SYS (TCP, port 1702):
    #     unmute: f'set Mic{ch} Mute 0\n'
    #     mute:   f'set Mic{ch} Mute 1\n'
    #
    #   Biamp Tesira (TCP, port 44100):
    #     unmute: f'DEVICE set inputMute {ch} false\n'
    #     mute:   f'DEVICE set inputMute {ch} true\n'
    #
    #   BSS Soundweb London (UDP, port 1023):
    #     # Use BLU-link or BSS protocol binary frames
    #
    #   AMX NetLinx (via SEND_COMMAND to a virtual device):
    #     send_string dvDSP, "'MUTE OFF ', itoa(ch)"  # unmute
    #     send_string dvDSP, "'MUTE ON  ', itoa(ch)"  # mute
    #
    #   Example TCP helper (add __init__ socket and call from below):
    #     self._dsp_sock.sendall(f'set Mic{ch} Mute 0\n'.encode())

    def _dsp_unmute(self, ch: int):
        """Unmute DSP channel for microphone <ch>."""
        # TODO: Send unmute command to DSP
        log.info("[DSP] Unmute ch=%d  ← implement DSP command here", ch)

    def _dsp_mute(self, ch: int):
        """Mute DSP channel for microphone <ch>."""
        # TODO: Send mute command to DSP
        log.info("[DSP] Mute   ch=%d  ← implement DSP command here", ch)

    # ── External control API ──────────────────────────────────────────────────

    def external_mic_on(self, ch: int):
        """Force-activate a mic (called from AMX panel or external control)."""
        with self._lock:
            self._activate(ch)
        log.info("External MIC_ON  ch=%d", ch)

    def external_mic_off(self, ch: int):
        """Force-deactivate a mic (called from AMX panel or external control)."""
        with self._lock:
            self._deactivate(ch)
        log.info("External MIC_OFF ch=%d", ch)

    def set_max_mics(self, limit: int):
        """
        Change the maximum simultaneous mics at runtime.
        limit=0 means unlimited.
        If currently over the new limit, oldest non-exception mics are evicted.
        """
        if limit < 0:
            log.warning("Ignoring invalid limit: %d", limit)
            return
        with self._lock:
            old = self.max_mics
            self.max_mics = limit
            log.info(
                "Max mics changed: %s → %s",
                old if old > 0 else "unlimited",
                limit if limit > 0 else "unlimited",
            )
            # Evict excess if currently over new limit
            if limit > 0:
                while len(self._active) > limit:
                    candidate = self._find_evict_candidate()
                    if candidate is None:
                        break
                    self._deactivate(candidate)
                    log.info("Evicted after limit reduction: ch=%d", candidate)

    def set_exception(self, ch: int, exempt: bool):
        """Add or remove a channel from the FIFO eviction exception list."""
        with self._lock:
            if exempt:
                self._exceptions.add(ch)
                log.info("Exception added:   ch=%d", ch)
            else:
                self._exceptions.discard(ch)
                log.info("Exception removed: ch=%d", ch)

    def get_status(self) -> dict:
        """Return current controller state."""
        with self._lock:
            return {
                "max_mics"  : self.max_mics if self.max_mics > 0 else "unlimited",
                "active"    : list(self._active),
                "exceptions": sorted(self._exceptions),
            }

    def _broadcast_status(self):
        st = self.get_status()
        active_str = ",".join(str(c) for c in st["active"]) or "none"
        except_str = ",".join(str(c) for c in st["exceptions"]) or "none"
        self._send(
            f"MIC_STATUS_REPLY,limit={st['max_mics']},"
            f"active={active_str},exceptions={except_str}"
        )
        log.info("Status broadcast: %s", st)


# ─── Entry point ──────────────────────────────────────────────────────────────

def main():
    controller = MicController(
        device_id=DEVICE_ID,
        max_mics=MAX_MICS_DEFAULT,
    )
    controller.start()

    log.info("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
    log.info("Listening on %s:%d", MCAST_GROUP, MCAST_PORT)
    log.info("External commands (send to same multicast group):")
    log.info("  MIC_ON,0,<ch>       Turn on mic channel")
    log.info("  MIC_OFF,0,<ch>      Turn off mic channel")
    log.info("  MIC_LIMIT,<n>       Set max simultaneous mics (0=unlimited)")
    log.info("  MIC_EXCEPT,<ch>,1   Mark channel as eviction exception")
    log.info("  MIC_EXCEPT,<ch>,0   Remove exception")
    log.info("  MIC_STATUS          Broadcast current status")
    log.info("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

    try:
        while True:
            time.sleep(10)
            st = controller.get_status()
            log.info(
                "Status | active=%s  limit=%s  exceptions=%s",
                st["active"], st["max_mics"], st["exceptions"],
            )
    except KeyboardInterrupt:
        pass
    finally:
        controller.stop()


if __name__ == "__main__":
    main()
