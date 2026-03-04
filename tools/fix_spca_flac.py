#!/usr/bin/env python3
"""
fix_spca_flac.py — SpCA 由来の FLAC ファイルに XOR 復号を適用する

SpCA フォーマットでは、フレーム0 は平文だがフレーム1以降は
XOR キー [0xEF, 0x42, 0x12, 0xBC] の4通りローテーションで暗号化されている。

使い方:
    python3 tools/fix_spca_flac.py data/rhodes-classic/audio/
"""

import struct
import sys
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor

XOR_BASE_KEY = bytes([0xEF, 0x42, 0x12, 0xBC])


def _find_encrypted_frame1(data: bytes, search_start: int,
                           max_scan: int = 500_000) -> tuple[int, int]:
    """XOR 暗号化された FLAC フレーム sync を探す"""
    search_end = min(len(data) - 4, search_start + max_scan)
    for pos in range(search_start, search_end):
        for rot in range(4):
            b0 = data[pos]     ^ XOR_BASE_KEY[rot]
            b1 = data[pos + 1] ^ XOR_BASE_KEY[(rot + 1) % 4]
            if b0 == 0xFF and (b1 & 0xFE) == 0xF8:
                # ヘッダーの妥当性チェック: blocksize と sample_rate
                b2 = data[pos + 2] ^ XOR_BASE_KEY[(rot + 2) % 4]
                bs_code = (b2 >> 4) & 0x0F
                sr_code = b2 & 0x0F
                # 典型的な値: blocksize=4096(0xC), sample_rate=44100(0x9)
                if bs_code >= 1 and sr_code >= 1 and sr_code != 15:
                    return pos, rot
    return -1, -1


def fix_file(path: Path) -> bool:
    """1ファイルの XOR 復号を適用（インプレース）"""
    data = bytearray(path.read_bytes())

    # fLaC チェック
    if data[:4] != b'fLaC':
        return False

    # メタデータブロック読み飛ばし
    pos = 4
    while pos < len(data):
        if pos + 4 > len(data):
            return False
        is_last = (data[pos] & 0x80) != 0
        blen = int.from_bytes(data[pos+1:pos+4], 'big')
        pos += 4 + blen
        if is_last:
            break

    audio_start = pos

    # 暗号化フレーム1 を探す
    enc_pos, enc_rot = _find_encrypted_frame1(bytes(data), audio_start + 10)
    if enc_pos == -1:
        # 暗号化されていない or 探索失敗
        return False

    # フレーム1以降を XOR 復号
    key = XOR_BASE_KEY[enc_rot:] + XOR_BASE_KEY[:enc_rot]
    for i in range(enc_pos, len(data)):
        data[i] ^= key[(i - enc_pos) % 4]

    path.write_bytes(data)
    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: fix_spca_flac.py <audio_dir>")
        sys.exit(1)

    audio_dir = Path(sys.argv[1])
    files = sorted(audio_dir.glob("*.flac"))
    print(f"{len(files)} ファイルを処理中...")

    fixed = 0
    skipped = 0
    with ProcessPoolExecutor() as pool:
        results = pool.map(fix_file, files)
        for ok in results:
            if ok:
                fixed += 1
            else:
                skipped += 1

    print(f"完了: {fixed} 修正, {skipped} スキップ")


if __name__ == "__main__":
    main()
