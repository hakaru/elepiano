"""ALSA Sequencer 仮想ポートから CC/PC を elepiano に送信する"""

import ctypes
import ctypes.util

# --- ALSA Sequencer C bindings (ctypes) ---
_lib = ctypes.CDLL(ctypes.util.find_library("asound") or "libasound.so.2")


class _CtrlEv(ctypes.Structure):
    """snd_seq_ev_ctrl_t: channel(1) + pad(3) + param(4) + value(4) = 12 bytes"""
    _fields_ = [
        ("channel", ctypes.c_uint8),
        ("_pad", ctypes.c_uint8 * 3),
        ("param", ctypes.c_uint32),
        ("value", ctypes.c_int32),
    ]


class _SeqData(ctypes.Union):
    _fields_ = [
        ("control", _CtrlEv),
        ("raw8", ctypes.c_uint8 * 12),
    ]


class _snd_seq_event_t(ctypes.Structure):
    """snd_seq_event_t (28 bytes)"""
    _fields_ = [
        ("type", ctypes.c_uint8),
        ("flags", ctypes.c_uint8),
        ("tag", ctypes.c_uint8),
        ("queue", ctypes.c_uint8),
        ("time", ctypes.c_uint8 * 8),
        ("source", ctypes.c_uint8 * 2),  # snd_seq_addr_t {client, port}
        ("dest", ctypes.c_uint8 * 2),    # snd_seq_addr_t {client, port}
        ("data", _SeqData),
    ]


# ALSA Seq event types
SND_SEQ_EVENT_CONTROLLER = 10
SND_SEQ_EVENT_PGMCHANGE = 11

# --- ALSA 関数シグネチャ宣言 ---
_lib.snd_seq_open.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
_lib.snd_seq_open.restype = ctypes.c_int
_lib.snd_seq_set_client_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_lib.snd_seq_client_id.argtypes = [ctypes.c_void_p]
_lib.snd_seq_client_id.restype = ctypes.c_int
_lib.snd_seq_create_simple_port.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint]
_lib.snd_seq_create_simple_port.restype = ctypes.c_int
_lib.snd_seq_connect_to.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.snd_seq_connect_to.restype = ctypes.c_int
_lib.snd_seq_disconnect_to.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.snd_seq_disconnect_to.restype = ctypes.c_int
_lib.snd_seq_event_output_direct.argtypes = [ctypes.c_void_p, ctypes.POINTER(_snd_seq_event_t)]
_lib.snd_seq_event_output_direct.restype = ctypes.c_int
_lib.snd_seq_close.argtypes = [ctypes.c_void_p]
_lib.snd_seq_close.restype = ctypes.c_int
_lib.snd_seq_client_info_malloc.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
_lib.snd_seq_client_info_malloc.restype = ctypes.c_int
_lib.snd_seq_client_info_free.argtypes = [ctypes.c_void_p]
_lib.snd_seq_client_info_set_client.argtypes = [ctypes.c_void_p, ctypes.c_int]
_lib.snd_seq_query_next_client.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
_lib.snd_seq_query_next_client.restype = ctypes.c_int
_lib.snd_seq_client_info_get_name.argtypes = [ctypes.c_void_p]
_lib.snd_seq_client_info_get_name.restype = ctypes.c_char_p
_lib.snd_seq_client_info_get_client.argtypes = [ctypes.c_void_p]
_lib.snd_seq_client_info_get_client.restype = ctypes.c_int


class AlsaMidiSender:
    """ALSA Seq 仮想ポートを作成し CC/PC イベントを elepiano に直接送信する"""

    def __init__(self, client_name: str = "ble-bridge"):
        self._seq = ctypes.c_void_p()
        rc = _lib.snd_seq_open(
            ctypes.byref(self._seq), b"default", 3, 0  # SND_SEQ_OPEN_DUPLEX
        )
        if rc < 0:
            raise RuntimeError(f"snd_seq_open failed: {rc}")

        _lib.snd_seq_set_client_name(self._seq, client_name.encode())

        self._port = _lib.snd_seq_create_simple_port(
            self._seq, b"BLE CC",
            0x01 | 0x20,  # READ | SUBS_READ (output port)
            0x02 | 0x04 | 0x100000,  # MIDI_GENERIC | SOFTWARE | APPLICATION
        )
        if self._port < 0:
            raise RuntimeError(f"snd_seq_create_simple_port failed: {self._port}")

        self._client_id = _lib.snd_seq_client_id(self._seq)
        print(f"[AlsaMidiSender] client={self._client_id} port={self._port}")

        # elepiano に直接接続（dest に elepiano の client:port を使う）
        self._dest_client = -1
        self._dest_port = 0
        self._connect_to_elepiano()

    def _find_elepiano_client(self) -> int:
        """'elepiano' ALSA client ID を返す（ctypes 直接呼び出し）。見つからなければ -1"""
        info = ctypes.c_void_p()
        if _lib.snd_seq_client_info_malloc(ctypes.byref(info)) < 0:
            return -1
        try:
            _lib.snd_seq_client_info_set_client(info, -1)
            while _lib.snd_seq_query_next_client(self._seq, info) >= 0:
                name = _lib.snd_seq_client_info_get_name(info)
                if name and b"elepiano" in name:
                    return _lib.snd_seq_client_info_get_client(info)
            return -1
        except Exception as e:
            print(f"[AlsaMidiSender] find failed: {e}")
            return -1
        finally:
            _lib.snd_seq_client_info_free(info)

    def _connect_to_elepiano(self) -> None:
        """'elepiano' クライアントを探して snd_seq_connect_to で接続"""
        client_id = self._find_elepiano_client()
        if client_id < 0:
            print("[AlsaMidiSender] WARNING: elepiano not found")
            return
        if client_id == self._dest_client:
            return  # 既に接続済み
        # 旧接続を切断
        if self._dest_client >= 0:
            _lib.snd_seq_disconnect_to(self._seq, self._port,
                                       self._dest_client, self._dest_port)
        # 新接続
        rc = _lib.snd_seq_connect_to(self._seq, self._port, client_id, 0)
        if rc >= 0:
            self._dest_client = client_id
            self._dest_port = 0
            print(f"[AlsaMidiSender] connected to elepiano (client {client_id})")
        else:
            print(f"[AlsaMidiSender] connect_to failed: {rc}")

    def reconnect(self) -> None:
        """elepiano の client ID が変わっていれば再接続"""
        self._connect_to_elepiano()

    def _make_event(self, event_type: int) -> _snd_seq_event_t:
        ev = _snd_seq_event_t()
        ctypes.memset(ctypes.byref(ev), 0, ctypes.sizeof(ev))
        ev.type = event_type
        ev.flags = 0  # SND_SEQ_TIME_STAMP_TICK | SND_SEQ_EVENT_LENGTH_FIXED
        ev.queue = 253  # SND_SEQ_QUEUE_DIRECT
        ev.source[0] = self._client_id & 0xFF
        ev.source[1] = self._port & 0xFF
        # 直接 elepiano に送信
        if self._dest_client >= 0:
            ev.dest[0] = self._dest_client & 0xFF
            ev.dest[1] = self._dest_port & 0xFF
        else:
            ev.dest[0] = 254  # SND_SEQ_ADDRESS_SUBSCRIBERS
            ev.dest[1] = 0
        return ev

    def send_cc(self, channel: int, cc: int, value: int) -> None:
        ev = self._make_event(SND_SEQ_EVENT_CONTROLLER)
        ev.data.control.channel = channel
        ev.data.control.param = cc
        ev.data.control.value = value
        _lib.snd_seq_event_output_direct(self._seq, ctypes.byref(ev))

    def send_program_change(self, channel: int, program: int) -> None:
        ev = self._make_event(SND_SEQ_EVENT_PGMCHANGE)
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
