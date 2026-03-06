"""ALSA Sequencer 仮想ポートから CC/PC を elepiano に送信する"""

import ctypes
import ctypes.util

# --- ALSA Sequencer C bindings (ctypes) ---
_lib = ctypes.CDLL(ctypes.util.find_library("asound") or "libasound.so.2")


class _snd_seq_event_t(ctypes.Structure):
    """最小限の snd_seq_event_t 構造体"""

    class _Data(ctypes.Union):
        class _CtrlEv(ctypes.Structure):
            _fields_ = [
                ("channel", ctypes.c_uint),
                ("unused1", ctypes.c_uint),
                ("unused2", ctypes.c_uint),
                ("param", ctypes.c_uint),
                ("value", ctypes.c_int),
            ]

        _fields_ = [
            ("control", _CtrlEv),
            ("raw8", ctypes.c_uint8 * 12),
        ]

    _fields_ = [
        ("type", ctypes.c_uint8),
        ("flags", ctypes.c_uint8),
        ("tag", ctypes.c_uint8),
        ("queue", ctypes.c_uint8),
        ("time", ctypes.c_uint8 * 8),  # snd_seq_timestamp_t
        ("source", ctypes.c_uint8 * 2),  # snd_seq_addr_t
        ("dest", ctypes.c_uint8 * 2),  # snd_seq_addr_t
        ("data", _Data),
    ]


# ALSA Seq event types
SND_SEQ_EVENT_CONTROLLER = 10
SND_SEQ_EVENT_PGMCHANGE = 11


class AlsaMidiSender:
    """ALSA Seq 仮想ポートを作成し CC/PC イベントを送信する"""

    def __init__(self, client_name: str = "ble-bridge"):
        self._seq = ctypes.c_void_p()
        rc = _lib.snd_seq_open(
            ctypes.byref(self._seq),
            b"default",
            3,  # SND_SEQ_OPEN_DUPLEX
            0,
        )
        if rc < 0:
            raise RuntimeError(f"snd_seq_open failed: {rc}")

        _lib.snd_seq_set_client_name(self._seq, client_name.encode())

        self._port = _lib.snd_seq_create_simple_port(
            self._seq,
            b"BLE CC",
            0x40 | 0x20,  # WRITE | SUBS_WRITE -> 他から見て READ | SUBS_READ
            0x01 | 0x02 | 0x04,  # SOFTWARE | MIDI_GENERIC | APPLICATION -> MIDI_GENERIC | SOFTWARE | APPLICATION
        )
        # Actually we need OUTPUT capabilities:
        # SND_SEQ_PORT_CAP_READ=1 | SND_SEQ_PORT_CAP_SUBS_READ=32 = 0x21
        # SND_SEQ_PORT_TYPE_MIDI_GENERIC=2 | SOFTWARE=4 | APPLICATION=0x100000 = 0x100006
        _lib.snd_seq_delete_simple_port(self._seq, self._port)
        self._port = _lib.snd_seq_create_simple_port(
            self._seq,
            b"BLE CC",
            0x01 | 0x20,  # READ | SUBS_READ (output port)
            0x02 | 0x04 | 0x100000,  # MIDI_GENERIC | SOFTWARE | APPLICATION
        )
        if self._port < 0:
            raise RuntimeError(f"snd_seq_create_simple_port failed: {self._port}")

        self._client_id = _lib.snd_seq_client_id(self._seq)
        print(f"[AlsaMidiSender] client={self._client_id} port={self._port}")

    def send_cc(self, channel: int, cc: int, value: int) -> None:
        ev = _snd_seq_event_t()
        ctypes.memset(ctypes.byref(ev), 0, ctypes.sizeof(ev))
        ev.type = SND_SEQ_EVENT_CONTROLLER
        ev.flags = 0x01  # SND_SEQ_EVENT_LENGTH_FIXED
        ev.queue = 253  # SND_SEQ_QUEUE_DIRECT
        ev.source.raw8 = (ctypes.c_uint8 * 2)(self._port, 0)  # type: ignore
        ev.dest.raw8 = (ctypes.c_uint8 * 2)(253, 253)  # type: ignore  # SUBSCRIBERS
        ev.data.control.channel = channel
        ev.data.control.param = cc
        ev.data.control.value = value
        _lib.snd_seq_event_output_direct(self._seq, ctypes.byref(ev))

    def send_program_change(self, channel: int, program: int) -> None:
        ev = _snd_seq_event_t()
        ctypes.memset(ctypes.byref(ev), 0, ctypes.sizeof(ev))
        ev.type = SND_SEQ_EVENT_PGMCHANGE
        ev.flags = 0x01
        ev.queue = 253
        ev.source.raw8 = (ctypes.c_uint8 * 2)(self._port, 0)  # type: ignore
        ev.dest.raw8 = (ctypes.c_uint8 * 2)(253, 253)  # type: ignore
        ev.data.control.channel = channel
        ev.data.control.value = program
        _lib.snd_seq_event_output_direct(self._seq, ctypes.byref(ev))

    def close(self) -> None:
        if self._seq:
            _lib.snd_seq_close(self._seq)
            self._seq = None


if __name__ == "__main__":
    import time

    sender = AlsaMidiSender()
    print("Sending CC7=64 (volume half) in 2 seconds...")
    time.sleep(2)
    sender.send_cc(0, 7, 64)
    print("Sent. Check elepiano volume.")
    sender.close()
