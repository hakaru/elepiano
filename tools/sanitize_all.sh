#!/bin/bash
# SpCA 抽出 FLAC の一括サニタイズ
# Usage: ./tools/sanitize_all.sh <input_dir> <output_dir> [--flac]
#
# 処理:
#   1. sanitize_flac で dr_flac デコード + PCM バースト除去 → WAV
#   2. --flac 指定時: 標準 flac エンコーダで再エンコード
#
# 例:
#   ./tools/sanitize_all.sh data/rhodes-classic/audio data/rhodes-classic-clean/audio --flac

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <input_dir> <output_dir> [--flac]" >&2
    exit 1
fi

INPUT_DIR="$1"
OUTPUT_DIR="$2"
REENCODE_FLAC=false

for arg in "${@:3}"; do
    case "$arg" in
        --flac) REENCODE_FLAC=true ;;
    esac
done

# sanitize_flac バイナリの場所
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SANITIZE_BIN="$PROJECT_DIR/build/sanitize_flac"

if [ ! -x "$SANITIZE_BIN" ]; then
    echo "sanitize_flac が見つかりません。ビルドします..." >&2
    cd "$PROJECT_DIR"
    g++ -O2 -std=c++17 -o build/sanitize_flac \
        tools/sanitize_flac.cpp src/flac_decoder.cpp \
        -I src 2>&1
    echo "ビルド完了: $SANITIZE_BIN" >&2
fi

mkdir -p "$OUTPUT_DIR"

# 統計
total=0
ok=0
burst_files=0
silent=0
errors=0

for flac_file in "$INPUT_DIR"/*.flac; do
    [ -f "$flac_file" ] || continue
    total=$((total + 1))

    basename="$(basename "$flac_file" .flac)"

    if [ "$REENCODE_FLAC" = true ]; then
        wav_tmp="$OUTPUT_DIR/${basename}.tmp.wav"
        out_file="$OUTPUT_DIR/${basename}.flac"
    else
        wav_tmp=""
        out_file="$OUTPUT_DIR/${basename}.wav"
    fi

    # サニタイズ実行
    target="${wav_tmp:-$out_file}"
    set +e
    output=$("$SANITIZE_BIN" "$flac_file" "$target" --stats 2>&1)
    ret=$?
    set -e

    if [ $ret -eq 0 ]; then
        # バースト検出があったかチェック
        if echo "$output" | grep -q "bursts:.*[1-9]"; then
            burst_files=$((burst_files + 1))
            echo "[BURST] $basename: $output" >&2
        fi

        # FLAC 再エンコード
        if [ "$REENCODE_FLAC" = true ]; then
            if flac --silent --best -f -o "$out_file" "$wav_tmp" 2>/dev/null; then
                rm -f "$wav_tmp"
            else
                echo "[WARN] FLAC エンコード失敗: $basename (WAV を保持)" >&2
                mv "$wav_tmp" "$OUTPUT_DIR/${basename}.wav"
            fi
        fi

        ok=$((ok + 1))
    elif [ $ret -eq 2 ]; then
        # 全ゼロ（CRC-16 ミュートで無音化）→ スキップ
        silent=$((silent + 1))
        rm -f "$target"
    else
        echo "[ERROR] $basename: $output" >&2
        errors=$((errors + 1))
        rm -f "$target"
    fi

    # 進捗表示（100 件ごと）
    if [ $((total % 100)) -eq 0 ]; then
        echo "  進捗: $total 件処理済み..." >&2
    fi
done

echo "" >&2
echo "=== サニタイズ完了 ===" >&2
echo "  合計:       $total 件" >&2
echo "  成功:       $ok 件" >&2
echo "  バースト修正: $burst_files 件" >&2
echo "  無音スキップ: $silent 件" >&2
echo "  エラー:     $errors 件" >&2
echo "  出力先:     $OUTPUT_DIR" >&2
