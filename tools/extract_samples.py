#!/usr/bin/env python3
"""
extract_samples.py - SpCA (.db) からサンプルを抽出・デコードするツール

対象: Rhodes - Classic Pattern 1 サンプル
SpCA フォーマット:
  - SpCA magic → fLaC に置換
  - フレーム1以降: XOR キー [ef 42 12 bc] の4通りローテーションを適用
"""

import struct
import json
import re
import subprocess
import sys
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
    """FLAC フレーム同期ワード 0xFF F8/F9 を探す"""
    i = start
    while i < len(data) - 1:
        if data[i] == 0xFF and (data[i+1] & 0xFE) == 0xF8:
            return i
        i += 1
    return -1


def _crc8(data: bytes) -> int:
    """CRC-8 (FLAC フレームヘッダー用)"""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def _find_frame_header_size(data: bytes, pos: int) -> int:
    """
    pos から始まる FLAC フレームヘッダーのバイト長を推定する。
    最小構造: sync(2) + blocksize_samplerate(1) + channelwidth(1) + sample_number(可変) + CRC8(1)
    """
    if pos + 4 > len(data):
        return -1
    # バイト2: ブロックサイズ/サンプルレートフィールド
    byte2 = data[pos + 2]
    byte3 = data[pos + 3]

    # sample/frame number (UTF-8 encoded)
    idx = pos + 4
    b0 = data[idx] if idx < len(data) else 0
    # UTF-8 先頭バイトで可変長を判定
    if b0 < 0x80:
        num_bytes = 1
    elif b0 < 0xC0:
        num_bytes = 1  # 不正だが仮
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

    # blocksize 末尾フィールド (byte2 上位4bit が 6 or 7)
    bs_code = (byte2 >> 4) & 0x0F
    if bs_code == 6:
        idx += 1
    elif bs_code == 7:
        idx += 2

    # sample rate 末尾フィールド (byte2 下位4bit が 12,13,14)
    sr_code = byte2 & 0x0F
    if sr_code == 12:
        idx += 1
    elif sr_code in (13, 14):
        idx += 2

    # CRC-8
    idx += 1
    return idx - pos


def decode_spca(spca_bytes: bytes) -> bytes:
    """
    SpCA バイト列を FLAC バイト列に変換する。
    1. SPCA_MAGIC → FLAC_MAGIC
    2. フレーム1以降: XOR キーの正しいローテーションを適用
    """
    if not spca_bytes.startswith(SPCA_MAGIC):
        raise ValueError(f"SpCA マジックバイトが見つかりません: {spca_bytes[:4].hex()}")

    # magic 置換
    result = bytearray(FLAC_MAGIC + spca_bytes[4:])

    # メタデータブロックを読み飛ばしてオーディオフレームの先頭を特定
    # fLaC(4) + 先頭メタデータブロック群をスキャン
    pos = 4
    while pos < len(result):
        if pos + 4 > len(result):
            break
        block_header = result[pos]
        is_last = (block_header & 0x80) != 0
        block_type = block_header & 0x7F
        block_len = struct.unpack_from(">I", bytes([0]) + bytes(result[pos+1:pos+4]))[0]
        pos += 4 + block_len
        if is_last:
            break

    audio_start = pos

    # フレーム0の位置を特定
    frame0_pos = _find_sync(result, audio_start)
    if frame0_pos == -1:
        raise ValueError("フレーム0の同期ワードが見つかりません")

    # フレーム0 ヘッダーサイズを求め、フレーム0の末尾まで進む
    hdr_size = _find_frame_header_size(result, frame0_pos)
    # フレーム0はそのまま（XOR なし）
    # フレーム1以降を XOR 解読する

    # フレーム1の位置を特定（フレーム0の後の最初の同期ワード）
    frame1_pos = _find_sync(result, frame0_pos + max(hdr_size, 1))
    if frame1_pos == -1:
        # オーディオフレームが1つのみ
        return bytes(result)

    # フレーム1が既に ff f8/f9 で始まっている場合は XOR 不要
    if result[frame1_pos] == 0xFF and (result[frame1_pos + 1] & 0xFE) == 0xF8:
        return bytes(result)

    # フレーム1以降の全バイトに XOR を適用する
    # まず4通りのローテーションを試し、先頭4バイトが ff f8/f9 xx xx になるものを選ぶ
    segment = bytes(result[frame1_pos:])
    best_rot = -1
    for rot in range(4):
        key = XOR_BASE_KEY[rot:] + XOR_BASE_KEY[:rot]
        candidate = bytes(b ^ key[i % 4] for i, b in enumerate(segment[:4]))
        if candidate[0] == 0xFF and (candidate[1] & 0xFE) == 0xF8:
            best_rot = rot
            break

    if best_rot == -1:
        raise ValueError("正しい XOR ローテーションが見つかりません")

    key = XOR_BASE_KEY[best_rot:] + XOR_BASE_KEY[:best_rot]
    decrypted = bytes(b ^ key[i % 4] for i, b in enumerate(segment))
    result[frame1_pos:] = decrypted

    return bytes(result)


# ============================================================
# FLAC → WAV 変換
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
    rr  = int(m.group(1))
    vel = int(m.group(2))
    note = int(m.group(3))
    return SampleMeta(midi_note=note, velocity_idx=vel, round_robin=rr, file_entry=None)


# ============================================================
# 速度マッピング（19段階）
# ============================================================
# velocity_idx → (vel_min, vel_max) の対応表
# Pattern 1 のサンプルに含まれる速度インデックスから推定する
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


# ============================================================
# メイン処理
# ============================================================
def extract(db_path: Path, output_dir: Path, pattern: str = "pattern1") -> None:
    print(f"[extract] 読み込み: {db_path}")
    db_bytes = db_path.read_bytes()

    print("[extract] XMLヘッダーをパース中...")
    entries, data_start = parse_db_header(db_bytes)
    print(f"[extract] {len(entries)} ファイルエントリ発見, データ開始: 0x{data_start:X}")

    # Pattern 1 のエントリだけ抽出
    metas: list[SampleMeta] = []
    for entry in entries:
        meta = parse_pattern1_name(entry.name)
        if meta is None:
            continue
        meta.file_entry = entry
        metas.append(meta)

    print(f"[extract] Pattern 1 エントリ数: {len(metas)}")
    if not metas:
        print("[extract] Pattern 1 エントリが見つかりません。ファイル名例:")
        for e in entries[:5]:
            print(f"  {e.name}")
        return

    # 速度インデックスからMIDI velocity範囲を生成
    vel_indices = [m.velocity_idx for m in metas]
    vel_ranges = build_velocity_range(vel_indices)

    audio_dir = output_dir / "audio"
    audio_dir.mkdir(parents=True, exist_ok=True)

    samples_info = []
    errors = []

    for i, meta in enumerate(metas):
        entry = meta.file_entry
        abs_offset = data_start + entry.offset
        spca_bytes = db_bytes[abs_offset: abs_offset + entry.size]

        out_stem = f"rr{meta.round_robin}_vel{meta.velocity_idx:03d}_n{meta.midi_note:03d}"
        flac_path = audio_dir / f"{out_stem}.flac"

        print(f"[{i+1:3d}/{len(metas)}] {entry.name} → {flac_path.name}", end=" ", flush=True)

        try:
            flac_bytes = decode_spca(spca_bytes)
            flac_path.write_bytes(flac_bytes)

            vel_min, vel_max = vel_ranges[meta.velocity_idx]
            samples_info.append({
                "midi_note":    meta.midi_note,
                "velocity_min": vel_min,
                "velocity_max": vel_max,
                "velocity_idx": meta.velocity_idx,
                "round_robin":  meta.round_robin,
                "file":         f"audio/{flac_path.name}"
            })
            print("OK")
        except Exception as e:
            print(f"ERROR: {e}")
            errors.append((entry.name, str(e)))

    # samples.json 出力
    samples_json = {
        "instrument":   "rhodes-classic",
        "sample_rate":  44100,
        "bit_depth":    24,
        "pattern":      "pattern1",
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

    if len(sys.argv) > 1:
        DB_PATH = Path(sys.argv[1])
    if len(sys.argv) > 2:
        OUTPUT_DIR = Path(sys.argv[2])

    extract(DB_PATH, OUTPUT_DIR)
