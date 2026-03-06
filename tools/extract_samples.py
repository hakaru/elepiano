#!/usr/bin/env python3
"""
extract_samples.py - SpCA (.db) からサンプルを抽出・デコードするツール

対象:
  pattern1      : Rhodes - Classic Pattern 1 サンプル (38ファイル, XOR暗号化なし)
  rhodes-attack : Rhodes - Classic CLR attack サンプル (1615ファイル, XOR暗号化あり)
  sustain       : Rhodes - Classic Sustain サンプル  (1615ファイル, XOR暗号化あり)
  wurl200a      : Wurlitzer 200A アタックサンプル    (1024ファイル, XOR暗号化なし)
  vvep          : Vintage Vibe EP アタックサンプル   (1539ファイル, XOR暗号化なし)
  rhodes-relf   : Rhodes - Classic CLR RelF  (release fade)    (1760ファイル, XOR暗号化なし)
  rhodes-relm   : Rhodes - Classic CLR RelMr3 (release mech)  (1760ファイル, XOR暗号化なし)
  rhodes-mchrel : Rhodes - Classic CLRMchRel (mechanical rel)  (1056ファイル, XOR暗号化なし)
  rhodes-mchr03 : Rhodes - Classic CLRMchr03 (mechanical atk)  (1408ファイル, XOR暗号化なし)

SpCA フォーマット:
  - SpCA magic → fLaC に置換
  - sustain のみフレーム1以降: XOR キー [ef 42 12 bc] の4通りローテーションを適用
"""

import struct
import json
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from dataclasses import dataclass, field

# ============================================================
# 定数
# ============================================================
XOR_BASE_KEY = bytes([0xEF, 0x42, 0x12, 0xBC])
SPCA_MAGIC   = b"SpCA"
FLAC_MAGIC   = b"fLaC"

# Pattern 1 ファイル名のパターン: "RR01 lacrmsp VV NN.wav"
PATTERN1_RE = re.compile(
    r"RR(\d+)\s+lacrmsp\s+(\d+)\s+(\d+)\.wav$",
    re.IGNORECASE
)

# Sustain サンプルのパターン: "RR01_SL01 CLR r10_60 v25.wav"
SUSTAIN_RE = re.compile(
    r"RR(\d+)_SL\d+\s+\w+\s+r\d+_(\d+)\s+v(\d+)\.wav$",
    re.IGNORECASE
)

# Wurlitzer 200A アタックのパターン: "NMWurl 60 a 50-64-o.wav"
# note=60, vel_min=50, vel_max=64
WURL200A_RE = re.compile(
    r"NMWurl\s+(\d+)\s+\w+\s+(\d+)-(\d+)-o\.wav$",
    re.IGNORECASE
)

# Vintage Vibe EP アタックのパターン: "RR01_SL01 VVEP r06_60 v12.wav"
# 高速バリアント: "RR01_SL01 VVEP r06_60 v15+.wav" (+ は RR=2, - は RR=3)
VVEP_RE = re.compile(
    r"RR(\d+)_SL\d+\s+VVEP\s+r\d+_(\d+)\s+v(\d+)([+-]?)\.wav$",
    re.IGNORECASE
)

# CLR RelF (Release Fade): "RR01_SL01 CLR RelF_60-96.wav"
CLR_RELF_RE = re.compile(
    r"RR(\d+)_SL\d+\s+CLR\s+RelF_(\d+)-(\d+)\.wav$",
    re.IGNORECASE
)

# CLR RelMr3 (Release Mechanical): "RR01_SL01 CLRRelMr3_60-96.wav"
CLR_RELM_RE = re.compile(
    r"RR(\d+)_SL\d+\s+CLRRelMr3_(\d+)-(\d+)\.wav$",
    re.IGNORECASE
)

# CLR MchRel (Mechanical Release): "RR01_SL02 CLRMchRel_60-100.wav"
CLR_MCHREL_RE = re.compile(
    r"RR(\d+)_SL\d+\s+CLRMchRel_(\d+)-(\d+)\.wav$",
    re.IGNORECASE
)

# CLR Mchr03 (Mechanical Attack r03): "RR01_SL02 CLRMchr03_60-116.wav"
CLR_MCHR03_RE = re.compile(
    r"RR(\d+)_SL\d+\s+CLRMchr03_(\d+)-(\d+)\.wav$",
    re.IGNORECASE
)

# Rhodes LA Custom attack: "RR01 lacrm 100 102.wav" (vel=100, note=102)
LACRM_RE = re.compile(
    r"RR(\d+)\s+lacrm\s+(\d+)\s+(\d+)\.wav$",
    re.IGNORECASE
)

# Rhodes LA Custom release: "RR01 lacr 100 109 rel.wav" (vel=100, note=109)
LACR_REL_RE = re.compile(
    r"RR(\d+)\s+lacr\s+(\d+)\s+(\d+)\s+rel\.wav$",
    re.IGNORECASE
)

# LA Custom C7 Grand attack (Pedal Up): "RR01_SL01LACPPUr09_100-100.wav" (note=100, vel=100)
LACPPU_RE = re.compile(
    r"RR(\d+)_SL\d+LACPPUr\d+_(\d+)-(\d+)\.wav$",
    re.IGNORECASE
)

# LA Custom C7 Grand release: "RR01 LACP Rel r08_100-100.wav" (note=100, vel=100)
LACP_REL_RE = re.compile(
    r"RR(\d+)\s+LACP\s+Rel\s+r\d+_(\d+)-(\d+)\.wav$",
    re.IGNORECASE
)

# Release モードの集合（vel_max → vel range 変換が必要なモード）
_RELEASE_MODES = frozenset({"rhodes-relf", "rhodes-relm", "rhodes-mchrel", "rhodes-mchr03"})

# ============================================================
# データクラス
# ============================================================
@dataclass
class FileEntry:
    name:   str
    offset: int
    size:   int

@dataclass
class SampleMeta:
    midi_note:    int
    velocity_idx: int
    round_robin:  int
    file_entry:   FileEntry
    # 明示的な velocity 範囲 (wurl200a など)。-1 = build_velocity_range で自動計算
    vel_min_explicit: int = -1
    vel_max_explicit: int = -1

# ============================================================
# XML ヘッダーパース
# ============================================================
def parse_db_header(data: bytes) -> tuple[list[FileEntry], int]:
    """
    .db バイト列から XML ヘッダーを読み取り、FileEntry のリストと
    データセクション開始オフセットを返す。
    """
    end_tag = b"</FileSystem>"
    idx = data.find(end_tag)
    if idx == -1:
        raise ValueError("</FileSystem> が見つかりません")
    xml_end = idx + len(end_tag)
    xml_bytes = data[:xml_end]
    xml_str = xml_bytes.decode("utf-8", errors="replace")

    # </FileSystem> の直後にある空白/改行をスキップしてデータセクション先頭を特定
    while xml_end < len(data) and data[xml_end] in (0x0A, 0x0D, 0x20):
        xml_end += 1

    entries = []
    # <FILE name="..." offset="..." size="..."/> を探す
    file_re = re.compile(
        r'<FILE\s+name="([^"]+)"\s+offset="(\d+)"\s+size="(\d+)"',
        re.IGNORECASE
    )
    for m in file_re.finditer(xml_str):
        name   = m.group(1)
        offset = int(m.group(2))
        size   = int(m.group(3))
        entries.append(FileEntry(name, offset, size))

    return entries, xml_end


# ============================================================
# SpCA デコーダー
# ============================================================
def _find_sync(data: bytes, start: int = 0) -> int:
    """FLAC フレーム同期ワード 0xFF F8/F9 を探す（平文用）"""
    i = start
    while i < len(data) - 1:
        if data[i] == 0xFF and (data[i+1] & 0xFE) == 0xF8:
            return i
        i += 1
    return -1


def _find_encrypted_frame1(data: bytes, search_start: int,
                            max_scan: int = 200_000) -> tuple[int, int]:
    """
    XOR 暗号化された FLAC フレーム同期ワードを探す（Sustain サンプル用）。

    data[pos] ^ XOR_BASE_KEY[rot] == 0xFF かつ
    data[pos+1] ^ XOR_BASE_KEY[(rot+1)%4] が 0xF8 または 0xF9
    になる (pos, rot) を返す。見つからなければ (-1, -1)。

    bs_code >= 1 かつ sr_code が有効 (1-14) であればマッチとする。
    """
    search_end = min(len(data) - 4, search_start + max_scan)
    for pos in range(search_start, search_end):
        for rot in range(4):
            b0 = data[pos]     ^ XOR_BASE_KEY[rot]
            b1 = data[pos + 1] ^ XOR_BASE_KEY[(rot + 1) % 4]
            if b0 == 0xFF and (b1 & 0xFE) == 0xF8:
                b2 = data[pos + 2] ^ XOR_BASE_KEY[(rot + 2) % 4]
                bs_code = (b2 >> 4) & 0x0F
                sr_code = b2 & 0x0F
                if bs_code >= 1 and sr_code >= 1 and sr_code != 15:
                    return pos, rot
    return -1, -1


def _find_frame_header_size(data: bytes, pos: int) -> int:
    """
    pos から始まる FLAC フレームヘッダーのバイト長を推定する。
    最小構造: sync(2) + blocksize_samplerate(1) + channelwidth(1) + sample_number(可変) + CRC8(1)
    """
    if pos + 4 > len(data):
        return -1
    byte2 = data[pos + 2]

    # sample/frame number (UTF-8 encoded)
    idx = pos + 4
    b0 = data[idx] if idx < len(data) else 0
    if b0 < 0x80:
        num_bytes = 1
    elif b0 < 0xC0:
        num_bytes = 1
    elif b0 < 0xE0:
        num_bytes = 2
    elif b0 < 0xF0:
        num_bytes = 3
    elif b0 < 0xF8:
        num_bytes = 4
    elif b0 < 0xFC:
        num_bytes = 5
    else:
        num_bytes = 6
    idx += num_bytes

    bs_code = (byte2 >> 4) & 0x0F
    if bs_code == 6:
        idx += 1
    elif bs_code == 7:
        idx += 2

    sr_code = byte2 & 0x0F
    if sr_code == 12:
        idx += 1
    elif sr_code in (13, 14):
        idx += 2

    idx += 1  # CRC-8
    return idx - pos


def decode_spca(spca_bytes: bytes, encrypted: bool = False) -> bytes:
    """
    SpCA バイト列を FLAC バイト列に変換する。

    encrypted=False (Pattern 1):
        SpCA magic → fLaC 置換のみ。XOR なし。
    encrypted=True (Sustain):
        Magic 置換後、フレーム1以降に XOR キーを適用。
    """
    if not spca_bytes.startswith(SPCA_MAGIC):
        raise ValueError(f"SpCA マジックバイトが見つかりません: {spca_bytes[:4].hex()}")

    # magic 置換
    result = bytearray(FLAC_MAGIC + spca_bytes[4:])

    if not encrypted:
        # dr_flac (DR_FLAC_NO_CRC) は CRC 不要 — CRC修復は偽sync位置を破損させるためスキップ
        return bytes(result)

    # --- Sustain: XOR 暗号化フレーム1を探して復号 ---

    # メタデータブロックを読み飛ばしてオーディオフレームの先頭を特定
    pos = 4
    while pos < len(result):
        if pos + 4 > len(result):
            break
        block_header = result[pos]
        is_last  = (block_header & 0x80) != 0
        block_len = struct.unpack_from(">I", bytes([0]) + bytes(result[pos+1:pos+4]))[0]
        pos += 4 + block_len
        if is_last:
            break

    audio_start = pos

    # フレーム0の位置を特定（平文）
    frame0_pos = _find_sync(result, audio_start)
    if frame0_pos == -1:
        raise ValueError("フレーム0の同期ワードが見つかりません")

    hdr_size    = _find_frame_header_size(result, frame0_pos)
    search_start = frame0_pos + max(hdr_size, 1)

    # XOR 暗号化されたフレーム1を探す
    enc_pos, enc_rot = _find_encrypted_frame1(bytes(result), search_start)
    if enc_pos == -1:
        # フレームが1つのみ（または解析不能）
        return bytes(result)

    # フレーム1以降を XOR 復号（インプレース）
    key = XOR_BASE_KEY[enc_rot:] + XOR_BASE_KEY[:enc_rot]
    for i in range(enc_pos, len(result)):
        result[i] ^= key[(i - enc_pos) % 4]

    return bytes(result)


# ============================================================
# FLAC → WAV 変換（デバッグ用）
# ============================================================
def flac_to_wav(flac_bytes: bytes, wav_path: Path) -> None:
    """ffmpeg を使い FLAC バイト列を WAV ファイルに変換する"""
    proc = subprocess.run(
        ["ffmpeg", "-y", "-i", "pipe:0", "-f", "wav", str(wav_path)],
        input=flac_bytes,
        capture_output=True
    )
    if proc.returncode != 0:
        raise RuntimeError(f"ffmpeg 変換失敗: {proc.stderr.decode(errors='replace')[-500:]}")


# ============================================================
# ファイル名パース
# ============================================================
def parse_pattern1_name(name: str) -> SampleMeta | None:
    """
    "RR01 lacrmsp 25 102.wav" → SampleMeta(midi_note=102, velocity_idx=25, round_robin=1)
    """
    m = PATTERN1_RE.search(name)
    if not m:
        return None
    rr   = int(m.group(1))
    vel  = int(m.group(2))
    note = int(m.group(3))
    return SampleMeta(midi_note=note, velocity_idx=vel, round_robin=rr, file_entry=None)


def parse_sustain_name(name: str) -> SampleMeta | None:
    """
    "RR01_SL01 CLR r10_60 v25.wav" → SampleMeta(midi_note=60, velocity_idx=25, round_robin=1)
    """
    m = SUSTAIN_RE.search(name)
    if not m:
        return None
    rr   = int(m.group(1))
    note = int(m.group(2))
    vel  = int(m.group(3))
    return SampleMeta(midi_note=note, velocity_idx=vel, round_robin=rr, file_entry=None)


def parse_vvep_name(name: str) -> SampleMeta | None:
    """
    "RR01_SL01 VVEP r06_60 v12.wav"  → SampleMeta(midi_note=60, velocity_idx=12, round_robin=1)
    "RR01_SL01 VVEP r06_60 v15+.wav" → SampleMeta(midi_note=60, velocity_idx=15, round_robin=2)
    "RR01_SL01 VVEP r06_60 v19-.wav" → SampleMeta(midi_note=60, velocity_idx=19, round_robin=3)
    """
    m = VVEP_RE.search(name)
    if not m:
        return None
    rr_base = int(m.group(1))
    note    = int(m.group(2))
    vel     = int(m.group(3))
    suffix  = m.group(4)
    # +/- バリアントはラウンドロビン2/3として扱う
    if suffix == "+":
        rr = 2
    elif suffix == "-":
        rr = 3
    else:
        rr = rr_base
    return SampleMeta(midi_note=note, velocity_idx=vel, round_robin=rr, file_entry=None)


def _parse_rr_note_velmax(pattern: re.Pattern, name: str) -> SampleMeta | None:
    """
    汎用パーサー: RR(d+)_SLd+ {TAG}_(d+)-(d+).wav 形式
    → SampleMeta(midi_note=note, velocity_idx=vel_max, round_robin=rr)
    vel_max は後処理で vel_min_explicit/vel_max_explicit に変換される。
    """
    m = pattern.search(name)
    if not m:
        return None
    return SampleMeta(
        midi_note=int(m.group(2)),
        velocity_idx=int(m.group(3)),
        round_robin=int(m.group(1)),
        file_entry=None
    )


def parse_clr_relf_name(name: str) -> SampleMeta | None:
    return _parse_rr_note_velmax(CLR_RELF_RE, name)


def parse_clr_relm_name(name: str) -> SampleMeta | None:
    return _parse_rr_note_velmax(CLR_RELM_RE, name)


def parse_clr_mchrel_name(name: str) -> SampleMeta | None:
    return _parse_rr_note_velmax(CLR_MCHREL_RE, name)


def parse_clr_mchr03_name(name: str) -> SampleMeta | None:
    return _parse_rr_note_velmax(CLR_MCHR03_RE, name)


def parse_lacrm_name(name: str) -> SampleMeta | None:
    """
    "RR01 lacrm 100 102.wav" → SampleMeta(midi_note=102, velocity_idx=100, round_robin=1)
    """
    m = LACRM_RE.search(name)
    if not m:
        return None
    rr   = int(m.group(1))
    vel  = int(m.group(2))
    note = int(m.group(3))
    return SampleMeta(midi_note=note, velocity_idx=vel, round_robin=rr, file_entry=None)


def parse_lacr_rel_name(name: str) -> SampleMeta | None:
    """
    "RR01 lacr 100 109 rel.wav" → SampleMeta(midi_note=109, velocity_idx=100, round_robin=1)
    """
    m = LACR_REL_RE.search(name)
    if not m:
        return None
    rr   = int(m.group(1))
    vel  = int(m.group(2))
    note = int(m.group(3))
    return SampleMeta(midi_note=note, velocity_idx=vel, round_robin=rr, file_entry=None)


def parse_lacppu_name(name: str) -> SampleMeta | None:
    """
    "RR01_SL01LACPPUr09_100-100.wav" → SampleMeta(midi_note=100, velocity_idx=100, round_robin=1)
    vel is explicit velocity value (not an index).
    """
    m = LACPPU_RE.search(name)
    if not m:
        return None
    rr   = int(m.group(1))
    note = int(m.group(2))
    vel  = int(m.group(3))
    return SampleMeta(midi_note=note, velocity_idx=vel, round_robin=rr, file_entry=None)


def parse_lacp_rel_name(name: str) -> SampleMeta | None:
    """
    "RR01 LACP Rel r08_100-100.wav" → SampleMeta(midi_note=100, velocity_idx=100, round_robin=1)
    """
    m = LACP_REL_RE.search(name)
    if not m:
        return None
    rr   = int(m.group(1))
    note = int(m.group(2))
    vel  = int(m.group(3))
    return SampleMeta(midi_note=note, velocity_idx=vel, round_robin=rr, file_entry=None)


def parse_wurl200a_name(name: str) -> SampleMeta | None:
    """
    "NMWurl 60 a 50-64-o.wav" → SampleMeta(midi_note=60, velocity_idx=50,
                                             vel_min_explicit=50, vel_max_explicit=64)
    velocity_idx には vel_min を使い、ファイル名生成に利用する。
    """
    m = WURL200A_RE.search(name)
    if not m:
        return None
    note    = int(m.group(1))
    vel_min = int(m.group(2))
    vel_max = int(m.group(3))
    return SampleMeta(
        midi_note=note, velocity_idx=vel_min, round_robin=1,
        file_entry=None,
        vel_min_explicit=vel_min, vel_max_explicit=vel_max
    )


# ============================================================
# 速度マッピング（等分割）
# ============================================================
def build_velocity_range(vel_indices: list[int]) -> dict[int, tuple[int, int]]:
    """
    速度インデックスのリストから、各インデックスが担当する
    MIDI velocity 範囲 (0-127) を等分で割り当てる。
    """
    sorted_idx = sorted(set(vel_indices))
    n = len(sorted_idx)
    ranges = {}
    for i, idx in enumerate(sorted_idx):
        v_min = round(i * 127 / n)
        v_max = round((i + 1) * 127 / n) - 1
        if i == n - 1:
            v_max = 127
        ranges[idx] = (v_min, v_max)
    return ranges


def build_vel_max_ranges(vel_max_values: list[int]) -> dict[int, tuple[int, int]]:
    """
    ファイル名に含まれる vel_max のリストから velocity 範囲を生成する。
    例: [43, 68, 96, 118, 127]
      → {43: (0, 43), 68: (44, 68), 96: (69, 96), 118: (97, 118), 127: (119, 127)}
    """
    sorted_maxes = sorted(set(vel_max_values))
    result: dict[int, tuple[int, int]] = {}
    prev = -1
    for vm in sorted_maxes:
        result[vm] = (prev + 1, vm)
        prev = vm
    return result


# ============================================================
# 並列ワーカー
# ============================================================
def _process_entry(args: tuple) -> dict:
    """
    1エントリを decode して FLAC ファイルに書き出す（ProcessPoolExecutor 用）。
    戻り値: {"ok": True, ...} or {"ok": False, "name": ..., "error": ...}
    """
    spca_bytes, flac_path_str, encrypted, midi_note, velocity_idx, round_robin, vel_min, vel_max = args
    flac_path = Path(flac_path_str)
    try:
        flac_bytes = decode_spca(spca_bytes, encrypted=encrypted)
        flac_path.write_bytes(flac_bytes)
        return {
            "ok": True,
            "midi_note":    midi_note,
            "velocity_min": vel_min,
            "velocity_max": vel_max,
            "velocity_idx": velocity_idx,
            "round_robin":  round_robin,
            "file":         f"audio/{flac_path.name}",
        }
    except Exception as e:
        return {"ok": False, "name": flac_path.name, "error": str(e)}


# ============================================================
# メイン処理
# ============================================================
def extract(db_path: Path, output_dir: Path, mode: str = "pattern1") -> None:
    """
    mode: "pattern1" または "sustain"
    """
    print(f"[extract] 読み込み: {db_path}  mode={mode}")
    db_bytes = db_path.read_bytes()

    print("[extract] XMLヘッダーをパース中...")
    entries, data_start = parse_db_header(db_bytes)
    print(f"[extract] {len(entries)} ファイルエントリ発見, データ開始: 0x{data_start:X}")

    # モードに応じてエントリを絞り込む
    if mode == "pattern1":
        parse_fn  = parse_pattern1_name
        encrypted = False
        label     = "Pattern 1"
    elif mode == "rhodes-attack":
        parse_fn  = parse_sustain_name
        encrypted = True
        label     = "Rhodes Attack"
    elif mode == "sustain":
        parse_fn  = parse_sustain_name
        encrypted = True
        label     = "Sustain"
    elif mode == "wurl200a":
        parse_fn  = parse_wurl200a_name
        encrypted = False
        label     = "Wurlitzer 200A"
    elif mode == "vvep":
        parse_fn  = parse_vvep_name
        encrypted = False
        label     = "Vintage Vibe EP"
    elif mode == "rhodes-relf":
        parse_fn  = parse_clr_relf_name
        encrypted = False
        label     = "Rhodes CLR RelF (Release Fade)"
    elif mode == "rhodes-relm":
        parse_fn  = parse_clr_relm_name
        encrypted = False
        label     = "Rhodes CLR RelMr3 (Release Mech)"
    elif mode == "rhodes-mchrel":
        parse_fn  = parse_clr_mchrel_name
        encrypted = False
        label     = "Rhodes CLR MchRel (Mechanical Release)"
    elif mode == "rhodes-mchr03":
        parse_fn  = parse_clr_mchr03_name
        encrypted = False
        label     = "Rhodes CLR Mchr03 (Mechanical Attack)"
    elif mode == "rhodes-la-attack":
        parse_fn  = parse_lacrm_name
        encrypted = False
        label     = "Rhodes LA Custom Attack"
    elif mode == "rhodes-la-rel":
        parse_fn  = parse_lacr_rel_name
        encrypted = False
        label     = "Rhodes LA Custom Release"
    elif mode == "c7grand-attack":
        parse_fn  = parse_lacppu_name
        encrypted = False
        label     = "LA Custom C7 Grand Attack (Pedal Up)"
    elif mode == "c7grand-rel":
        parse_fn  = parse_lacp_rel_name
        encrypted = False
        label     = "LA Custom C7 Grand Release"
    else:
        raise ValueError(f"未知のモード: {mode}")

    metas: list[SampleMeta] = []
    for entry in entries:
        meta = parse_fn(entry.name)
        if meta is None:
            continue
        meta.file_entry = entry
        metas.append(meta)

    print(f"[extract] {label} エントリ数: {len(metas)}")
    if not metas:
        print(f"[extract] {label} エントリが見つかりません。ファイル名例:")
        for e in entries[:5]:
            print(f"  {e.name}")
        return

    # Release モード: vel_max → vel range に変換してから explicit range として記録
    if mode in _RELEASE_MODES:
        vm_ranges = build_vel_max_ranges([m.velocity_idx for m in metas])
        for m in metas:
            m.vel_min_explicit = vm_ranges[m.velocity_idx][0]
            m.vel_max_explicit = m.velocity_idx  # velocity_idx == vel_max

    # velocity 範囲の決定
    # 明示的な vel_min/vel_max を持つ場合はそちらを優先し、
    # ない場合は build_velocity_range で自動計算する
    use_explicit_range = all(m.vel_min_explicit >= 0 for m in metas)
    if not use_explicit_range:
        vel_indices = [m.velocity_idx for m in metas]
        vel_ranges  = build_velocity_range(vel_indices)

    audio_dir = output_dir / "audio"
    audio_dir.mkdir(parents=True, exist_ok=True)

    # ワーカー引数リストを構築（db_bytes のスライスをここで切り出す）
    tasks = []
    for meta in metas:
        entry = meta.file_entry
        abs_offset = data_start + entry.offset
        spca_bytes = db_bytes[abs_offset: abs_offset + entry.size]
        out_stem  = f"rr{meta.round_robin}_vel{meta.velocity_idx:03d}_n{meta.midi_note:03d}"
        flac_path = audio_dir / f"{out_stem}.flac"
        if use_explicit_range:
            vel_min = meta.vel_min_explicit
            vel_max = meta.vel_max_explicit
        else:
            vel_min, vel_max = vel_ranges[meta.velocity_idx]
        tasks.append((spca_bytes, str(flac_path), encrypted,
                      meta.midi_note, meta.velocity_idx, meta.round_robin,
                      vel_min, vel_max))

    samples_info = []
    errors = []
    log_interval = max(1, len(tasks) // 20)  # 5% ごとにログ出力

    with ProcessPoolExecutor() as pool:
        for i, result in enumerate(pool.map(_process_entry, tasks)):
            if (i + 1) % log_interval == 0 or i + 1 == len(tasks):
                print(f"[{i+1:4d}/{len(tasks)}] {'OK' if result['ok'] else 'ERROR'}", flush=True)
            if result["ok"]:
                samples_info.append({k: v for k, v in result.items() if k != "ok"})
            else:
                errors.append((result["name"], result["error"]))

    # samples.json 出力
    instrument_name = {
        "pattern1":      "rhodes-classic",
        "rhodes-attack": "rhodes-classic",
        "sustain":       "rhodes-classic-sustain",
        "wurl200a":      "wurlitzer-200a",
        "vvep":          "vintage-vibe-ep",
        "rhodes-relf":   "rhodes-classic-relf",
        "rhodes-relm":   "rhodes-classic-relm",
        "rhodes-mchrel": "rhodes-classic-mchrel",
        "rhodes-mchr03": "rhodes-classic-mchr03",
        "rhodes-la-attack": "rhodes-la-custom",
        "rhodes-la-rel":    "rhodes-la-custom-rel",
        "c7grand-attack":   "la-custom-c7-grand",
        "c7grand-rel":      "la-custom-c7-grand-rel",
    }.get(mode, mode)
    samples_json = {
        "instrument":   instrument_name,
        "sample_rate":  44100,
        "bit_depth":    24,
        "pattern":      mode,
        "samples":      sorted(samples_info, key=lambda s: (s["midi_note"], s["velocity_idx"]))
    }
    json_path = output_dir / "samples.json"
    json_path.write_text(json.dumps(samples_json, indent=2, ensure_ascii=False))
    print(f"\n[extract] samples.json 書き出し完了: {json_path}")

    if errors:
        print(f"\n[extract] エラー {len(errors)} 件:")
        for name, err in errors:
            print(f"  {name}: {err}")
    else:
        print(f"[extract] 全 {len(metas)} 件 成功")


if __name__ == "__main__":
    DB_PATH = Path(
        "/Volumes/Media/AudioLibraries/Spectrasonics/STEAM/Keyscape/"
        "Soundsources/Factory/Keyscape Library/Keyboards/Rhodes - Classic.db"
    )
    OUTPUT_DIR = Path("/Volumes/Dev/DEVELOP/elepiano/data/rhodes")
    MODE = "pattern1"

    if len(sys.argv) > 1:
        DB_PATH = Path(sys.argv[1])
    if len(sys.argv) > 2:
        OUTPUT_DIR = Path(sys.argv[2])
    if len(sys.argv) > 3:
        MODE = sys.argv[3]

    extract(DB_PATH, OUTPUT_DIR, mode=MODE)
