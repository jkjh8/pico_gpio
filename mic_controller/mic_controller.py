#!/usr/bin/env python3
"""
Conference Gooseneck Microphone Controller
AMX MU Series External Control via Multicast

Pico Device Protocol (239.224.0.1:9000, UDP multicast):
  RX (device → controller): in,<device_id>,<channel>,<value>
  TX (controller → device): set,<device_id>,<channel>,<value>

External Control Commands (AMX MU → 239.224.0.1:9000):
  MIC_ON,<device_id>,<channel>            - Force turn on mic LED
  MIC_OFF,<device_id>,<channel>           - Force turn off mic LED
  MIC_LIMIT,<count>                       - Set max simultaneous mics (0 = unlimited)
  MIC_EXCEPT,<device_id>,<channel>,<1|0>  - Add/remove channel from eviction exception
  MIC_STATUS                              - Broadcast current status

Design:
  - Input ch N  = mic button (HC165 shift register, Pico input)
  - Output ch N = mic LED   (HC595 shift register, Pico output)
  - Button press toggles mic on/off
  - FIFO eviction: when active mics exceed limit, oldest non-exception mic is
    turned off automatically
  - DSP channel mapping: device_id=1 → ch 1-16, device_id=2 → ch 17-32, …
    Formula: dsp_ch = (device_id - 1) * MIC_CHANNELS + ch
"""

import socket
import struct
import threading
import time
import logging
from collections import deque
from typing import Set, Optional, Tuple

# ─── Configuration ────────────────────────────────────────────────────────────

MCAST_GROUP      = "239.224.0.1"
MCAST_PORT       = 9000
MCAST_TTL        = 4
DEVICE_ID        = 0            # 0 = accept all devices (multi-device mode)
MAX_MICS_DEFAULT = 2            # Default max simultaneous active mics
MIC_CHANNELS     = 16          # Channels per device (1-based: 1..16)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("mic_controller")

# Type alias: (device_id, channel)
MicKey = Tuple[int, int]


# ─── Controller ───────────────────────────────────────────────────────────────

class MicController:
    """
    Conference microphone controller.

    Responsibilities:
      1. Listen for mic button presses from Pico devices via multicast.
      2. Toggle mic LED and DSP mute/unmute accordingly.
      3. Enforce a configurable maximum simultaneous mic limit using FIFO eviction.
      4. Accept real-time control commands from AMX MU series (or any multicast client).
      5. Map per-device channels to global DSP channels:
           DSP ch = (device_id - 1) * MIC_CHANNELS + ch
    """

    def __init__(
        self,
        device_id: int = DEVICE_ID,
        max_mics: int = MAX_MICS_DEFAULT,
    ):
        # 0 = accept all devices; otherwise filter to this specific device_id
        self.device_id = device_id
        self.max_mics  = max_mics   # 0 = unlimited

        self._lock       = threading.Lock()
        self._active     : deque[MicKey] = deque()   # FIFO, oldest first; (dev_id, ch)
        self._exceptions : Set[MicKey]   = set()     # channels exempt from eviction

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
            "MicController started | device_id=%s  max_mics=%s",
            self.device_id if self.device_id != 0 else "ALL",
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
        parts = [p.strip() for p in msg.split(",")]
        cmd   = parts[0].upper()

        # ── Pico device input feedback ────────────────────────────────────────
        # Format: in,<device_id>,<channel>,<value>
        if cmd == "IN" and len(parts) == 4:
            try:
                dev_id = int(parts[1])
                ch     = int(parts[2])
                value  = int(parts[3])
            except ValueError:
                return
            # Filter: only handle messages for this controller's device_id.
            # device_id=0 means "accept all devices" (multi-device mode).
            if self.device_id != 0 and dev_id != self.device_id:
                log.debug("Ignored IN from device_id=%d (expected %d)", dev_id, self.device_id)
                return
            if value == 1:
                self._on_button_press(dev_id, ch)
            # Button release (value=0) is ignored; toggle happens on press only
            return

        # ── External control commands (AMX MU series or any multicast client) ─
        # Format: MIC_ON,<device_id>,<channel>  /  MIC_OFF,<device_id>,<channel>

        if cmd == "MIC_ON" and len(parts) >= 3:
            try:
                dev_id = int(parts[1])
                ch     = int(parts[2])
            except ValueError:
                return
            if self.device_id != 0 and dev_id != self.device_id:
                return
            self.external_mic_on(dev_id, ch)

        elif cmd == "MIC_OFF" and len(parts) >= 3:
            try:
                dev_id = int(parts[1])
                ch     = int(parts[2])
            except ValueError:
                return
            if self.device_id != 0 and dev_id != self.device_id:
                return
            self.external_mic_off(dev_id, ch)

        elif cmd == "MIC_LIMIT" and len(parts) >= 2:
            try:
                limit = int(parts[1])
            except ValueError:
                return
            self.set_max_mics(limit)

        elif cmd == "MIC_EXCEPT" and len(parts) >= 4:
            # Format: MIC_EXCEPT,<device_id>,<channel>,<1|0>
            try:
                dev_id = int(parts[1])
                ch     = int(parts[2])
                state  = int(parts[3])
            except ValueError:
                return
            self.set_exception(dev_id, ch, bool(state))

        elif cmd == "MIC_STATUS":
            self._broadcast_status()

    # ── DSP channel mapping ───────────────────────────────────────────────────

    def _dsp_channel(self, dev_id: int, ch: int) -> int:
        """
        Map a per-device channel to a global DSP channel number.
        device_id=1, ch=1  → DSP ch=1
        device_id=1, ch=16 → DSP ch=16
        device_id=2, ch=1  → DSP ch=17
        device_id=2, ch=16 → DSP ch=32
        device_id=0 (legacy/broadcast): return ch unchanged.
        """
        if dev_id == 0:
            return ch
        return (dev_id - 1) * MIC_CHANNELS + ch

    # ── Button event ──────────────────────────────────────────────────────────

    def _on_button_press(self, dev_id: int, ch: int):
        """Physical mic button pressed → toggle mic on/off."""
        key = (dev_id, ch)
        with self._lock:
            if key in self._active:
                self._deactivate(dev_id, ch)
                log.info("Mic OFF (button toggle) device_id=%d ch=%d  dsp_ch=%d",
                         dev_id, ch, self._dsp_channel(dev_id, ch))
            else:
                self._activate(dev_id, ch)
                log.info("Mic ON  (button press)  device_id=%d ch=%d  dsp_ch=%d",
                         dev_id, ch, self._dsp_channel(dev_id, ch))

    # ── Core activation logic ─────────────────────────────────────────────────

    def _activate(self, dev_id: int, ch: int):
        """
        Turn on mic channel, enforcing FIFO simultaneous-mic limit.
        Must be called with self._lock held.
        """
        key = (dev_id, ch)
        if key in self._active:
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
                evict_dev, evict_ch = candidate
                self._deactivate(evict_dev, evict_ch)
                log.info("Evicted (FIFO limit=%d) device_id=%d ch=%d  dsp_ch=%d",
                         self.max_mics, evict_dev, evict_ch,
                         self._dsp_channel(evict_dev, evict_ch))

        self._active.append(key)
        self._set_led(dev_id, ch, True)
        self._dsp_unmute(dev_id, ch)

    def _deactivate(self, dev_id: int, ch: int):
        """
        Turn off mic channel.
        Must be called with self._lock held.
        """
        key = (dev_id, ch)
        if key not in self._active:
            return
        try:
            self._active.remove(key)
        except ValueError:
            pass
        self._set_led(dev_id, ch, False)
        self._dsp_mute(dev_id, ch)

    def _find_evict_candidate(self) -> Optional[MicKey]:
        """Return oldest active mic not in the exception list, or None."""
        for key in self._active:   # deque iterates oldest → newest
            if key not in self._exceptions:
                return key
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

    def _set_led(self, dev_id: int, ch: int, on: bool):
        """Control mic LED via Pico GPIO output channel."""
        self._send(f"set,{dev_id},{ch},{1 if on else 0}")
        log.debug("LED device_id=%d ch=%d → %s", dev_id, ch, "ON" if on else "OFF")

    # ── DSP Integration ───────────────────────────────────────────────────────
    #
    # DSP channel = (device_id - 1) * MIC_CHANNELS + ch
    #   device_id=1 → DSP ch 1-16
    #   device_id=2 → DSP ch 17-32
    #   device_id=3 → DSP ch 33-48
    #
    # TODO: Implement DSP mute/unmute commands for your specific DSP processor.
    #
    # Common DSP protocols:
    #
    #   QSC Q-SYS (TCP, port 1702):
    #     unmute: f'set Mic{dsp_ch} Mute 0\n'
    #     mute:   f'set Mic{dsp_ch} Mute 1\n'
    #
    #   Biamp Tesira (TCP, port 44100):
    #     unmute: f'DEVICE set inputMute {dsp_ch} false\n'
    #     mute:   f'DEVICE set inputMute {dsp_ch} true\n'
    #
    #   BSS Soundweb London (UDP, port 1023):
    #     # Use BLU-link or BSS protocol binary frames
    #
    #   AMX NetLinx (via SEND_COMMAND to a virtual device):
    #     send_string dvDSP, "'MUTE OFF ', itoa(dsp_ch)"  # unmute
    #     send_string dvDSP, "'MUTE ON  ', itoa(dsp_ch)"  # mute
    #
    #   Example TCP helper (add __init__ socket and call from below):
    #     self._dsp_sock.sendall(f'set Mic{dsp_ch} Mute 0\n'.encode())

    def _dsp_unmute(self, dev_id: int, ch: int):
        """Unmute DSP channel for microphone (dev_id, ch)."""
        dsp_ch = self._dsp_channel(dev_id, ch)
        # TODO: Send unmute command to DSP using dsp_ch
        log.info("[DSP] Unmute device_id=%d ch=%d → dsp_ch=%d  ← implement DSP command here",
                 dev_id, ch, dsp_ch)

    def _dsp_mute(self, dev_id: int, ch: int):
        """Mute DSP channel for microphone (dev_id, ch)."""
        dsp_ch = self._dsp_channel(dev_id, ch)
        # TODO: Send mute command to DSP using dsp_ch
        log.info("[DSP] Mute   device_id=%d ch=%d → dsp_ch=%d  ← implement DSP command here",
                 dev_id, ch, dsp_ch)

    # ── External control API ──────────────────────────────────────────────────

    def external_mic_on(self, dev_id: int, ch: int):
        """Force-activate a mic (called from AMX panel or external control)."""
        with self._lock:
            self._activate(dev_id, ch)
        log.info("External MIC_ON  device_id=%d ch=%d  dsp_ch=%d",
                 dev_id, ch, self._dsp_channel(dev_id, ch))

    def external_mic_off(self, dev_id: int, ch: int):
        """Force-deactivate a mic (called from AMX panel or external control)."""
        with self._lock:
            self._deactivate(dev_id, ch)
        log.info("External MIC_OFF device_id=%d ch=%d  dsp_ch=%d",
                 dev_id, ch, self._dsp_channel(dev_id, ch))

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
                    evict_dev, evict_ch = candidate
                    self._deactivate(evict_dev, evict_ch)
                    log.info("Evicted after limit reduction: device_id=%d ch=%d", evict_dev, evict_ch)

    def set_exception(self, dev_id: int, ch: int, exempt: bool):
        """Add or remove a channel from the FIFO eviction exception list."""
        key = (dev_id, ch)
        with self._lock:
            if exempt:
                self._exceptions.add(key)
                log.info("Exception added:   device_id=%d ch=%d", dev_id, ch)
            else:
                self._exceptions.discard(key)
                log.info("Exception removed: device_id=%d ch=%d", dev_id, ch)

    def get_status(self) -> dict:
        """Return current controller state."""
        with self._lock:
            active_list = [
                {"device_id": d, "ch": c, "dsp_ch": self._dsp_channel(d, c)}
                for d, c in self._active
            ]
            except_list = [
                {"device_id": d, "ch": c, "dsp_ch": self._dsp_channel(d, c)}
                for d, c in sorted(self._exceptions)
            ]
            return {
                "max_mics"  : self.max_mics if self.max_mics > 0 else "unlimited",
                "active"    : active_list,
                "exceptions": except_list,
            }

    def _broadcast_status(self):
        st = self.get_status()
        active_str = ",".join(
            f"{m['device_id']}:{m['ch']}(dsp{m['dsp_ch']})" for m in st["active"]
        ) or "none"
        except_str = ",".join(
            f"{m['device_id']}:{m['ch']}" for m in st["exceptions"]
        ) or "none"
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
    log.info("device_id=0 → multi-device mode (accept all devices)")
    log.info("DSP ch mapping: device_id=1 ch1-16, device_id=2 ch17-32, ...")
    log.info("External commands (send to same multicast group):")
    log.info("  MIC_ON,<dev_id>,<ch>          Turn on mic channel")
    log.info("  MIC_OFF,<dev_id>,<ch>         Turn off mic channel")
    log.info("  MIC_LIMIT,<n>                 Set max simultaneous mics (0=unlimited)")
    log.info("  MIC_EXCEPT,<dev_id>,<ch>,1    Mark channel as eviction exception")
    log.info("  MIC_EXCEPT,<dev_id>,<ch>,0    Remove exception")
    log.info("  MIC_STATUS                    Broadcast current status")
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
