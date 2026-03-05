#!/usr/bin/env python3
"""WAV ファイルの自動波形解析ツール — elepiano ノイズ診断用

Usage: python3 tools/analyze_noise.py <wav_file> [--out report.txt]
"""
import sys
import struct
import os
import math
import subprocess

def read_wav(path):
    """WAV (S16LE, stereo) を読み込んで (samples, sample_rate, channels) を返す"""
    with open(path, "rb") as f:
        riff = f.read(4)
        if riff != b"RIFF":
            raise ValueError(f"Not a RIFF file: {riff}")
        f.read(4)  # file size
        wave = f.read(4)
        if wave != b"WAVE":
            raise ValueError(f"Not a WAVE file: {wave}")

        fmt_found = False
        sample_rate = 0
        channels = 0
        bits_per_sample = 0

        while True:
            chunk_id = f.read(4)
            if len(chunk_id) < 4:
                break
            chunk_size = struct.unpack("<I", f.read(4))[0]

            if chunk_id == b"fmt ":
                fmt_data = f.read(chunk_size)
                audio_fmt = struct.unpack("<H", fmt_data[0:2])[0]
                if audio_fmt != 1:
                    raise ValueError(f"Unsupported format: {audio_fmt} (only PCM)")
                channels = struct.unpack("<H", fmt_data[2:4])[0]
                sample_rate = struct.unpack("<I", fmt_data[4:8])[0]
                bits_per_sample = struct.unpack("<H", fmt_data[14:16])[0]
                fmt_found = True

            elif chunk_id == b"data":
                if not fmt_found:
                    raise ValueError("data chunk before fmt chunk")
                raw = f.read(chunk_size)
                if bits_per_sample == 16:
                    n = len(raw) // 2
                    samples = list(struct.unpack(f"<{n}h", raw))
                else:
                    raise ValueError(f"Unsupported bits: {bits_per_sample}")
                return samples, sample_rate, channels
            else:
                f.read(chunk_size)

    raise ValueError("No data chunk found")


def analyze(samples, sample_rate, channels):
    """解析を実行してレポート文字列を返す"""
    lines = []
    total = len(samples)
    num_frames = total // channels
    duration = num_frames / sample_rate

    lines.append("=" * 60)
    lines.append("elepiano ノイズ解析レポート")
    lines.append("=" * 60)
    lines.append(f"サンプルレート: {sample_rate} Hz")
    lines.append(f"チャンネル数:   {channels}")
    lines.append(f"フレーム数:     {num_frames}")
    lines.append(f"再生時間:       {duration:.2f} 秒")
    lines.append("")

    # --- 基本統計 ---
    lines.append("--- 基本統計 ---")
    peak = max(abs(s) for s in samples)
    rms = math.sqrt(sum(s * s for s in samples) / total)
    dc_offset = sum(samples) / total
    lines.append(f"Peak:      {peak} ({peak / 32767.0:.4f})")
    lines.append(f"RMS:       {rms:.1f} ({rms / 32767.0:.4f})")
    lines.append(f"DC offset: {dc_offset:.1f} ({dc_offset / 32767.0:.6f})")
    lines.append("")

    # --- クリップ率 ---
    lines.append("--- クリップ検出 ---")
    clip_count = sum(1 for s in samples if abs(s) >= 32767)
    clip_pct = clip_count / total * 100
    lines.append(f"クリップ数: {clip_count} ({clip_pct:.4f}%)")
    lines.append("")

    # --- クリック検出（隣接サンプル差分） ---
    lines.append("--- クリック検出（隣接サンプル差分 > 閾値） ---")
    threshold = 8000  # S16 で約 25% のジャンプ
    clicks = []
    for ch in range(channels):
        for i in range(1, num_frames):
            idx_prev = (i - 1) * channels + ch
            idx_curr = i * channels + ch
            diff = abs(samples[idx_curr] - samples[idx_prev])
            if diff > threshold:
                time_sec = i / sample_rate
                clicks.append((time_sec, ch, diff, samples[idx_curr]))

    lines.append(f"閾値: {threshold}")
    lines.append(f"検出数: {len(clicks)}")
    if clicks:
        lines.append("最初の20件:")
        for t, ch, diff, val in clicks[:20]:
            lines.append(f"  {t:.4f}s ch={ch} diff={diff} val={val}")
        if len(clicks) > 20:
            lines.append(f"  ... 他 {len(clicks) - 20} 件")
    lines.append("")

    # --- 異常区間検出（短区間 RMS の急変） ---
    lines.append("--- 異常区間検出（10ms 区間 RMS 急変） ---")
    window = int(sample_rate * 0.01)  # 10ms
    rms_values = []
    for start in range(0, num_frames - window, window):
        chunk = samples[start * channels:(start + window) * channels]
        r = math.sqrt(sum(s * s for s in chunk) / len(chunk)) if chunk else 0
        rms_values.append((start / sample_rate, r))

    anomalies = []
    for i in range(1, len(rms_values)):
        t_prev, r_prev = rms_values[i - 1]
        t_curr, r_curr = rms_values[i]
        if r_prev > 0:
            ratio = r_curr / r_prev
            if ratio > 5.0 or (r_prev > 100 and ratio < 0.1):
                anomalies.append((t_curr, r_prev, r_curr, ratio))

    lines.append(f"検出数: {len(anomalies)}")
    for t, rp, rc, ratio in anomalies[:20]:
        lines.append(f"  {t:.4f}s RMS {rp:.0f} -> {rc:.0f} (x{ratio:.1f})")
    lines.append("")

    # --- 時間推移（1秒ごとの RMS） ---
    lines.append("--- 時間推移（1秒ごとの RMS） ---")
    sec_window = sample_rate
    for sec in range(int(duration) + 1):
        start = sec * sec_window * channels
        end = min((sec + 1) * sec_window * channels, total)
        chunk = samples[start:end]
        if not chunk:
            break
        r = math.sqrt(sum(s * s for s in chunk) / len(chunk))
        bar = "#" * int(r / 500)
        lines.append(f"  {sec:4d}s  RMS={r:7.1f}  {bar}")
    lines.append("")

    # --- チャンネル別 DC offset ---
    if channels == 2:
        lines.append("--- チャンネル別統計 ---")
        for ch in range(channels):
            ch_samples = samples[ch::channels]
            ch_dc = sum(ch_samples) / len(ch_samples)
            ch_rms = math.sqrt(sum(s * s for s in ch_samples) / len(ch_samples))
            ch_peak = max(abs(s) for s in ch_samples)
            lines.append(f"  CH{ch}: DC={ch_dc:.1f} RMS={ch_rms:.1f} Peak={ch_peak}")
        lines.append("")

    return "\n".join(lines)


def generate_spectrogram(wav_path, out_png):
    """sox があればスペクトログラム PNG を生成"""
    try:
        subprocess.run(
            ["sox", wav_path, "-n", "spectrogram", "-o", out_png,
             "-t", "elepiano noise analysis", "-x", "1200", "-y", "513"],
            check=True, capture_output=True
        )
        return True
    except (FileNotFoundError, subprocess.CalledProcessError):
        return False


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <wav_file> [--out report.txt]", file=sys.stderr)
        sys.exit(1)

    wav_path = sys.argv[1]
    out_path = None
    for i, arg in enumerate(sys.argv):
        if arg == "--out" and i + 1 < len(sys.argv):
            out_path = sys.argv[i + 1]

    if not out_path:
        base = os.path.splitext(wav_path)[0]
        out_path = base + "_analysis.txt"

    samples, sample_rate, channels = read_wav(wav_path)
    report = analyze(samples, sample_rate, channels)

    with open(out_path, "w") as f:
        f.write(report)
    print(report)
    print(f"\nレポート保存: {out_path}")

    # スペクトログラム生成
    png_path = os.path.splitext(wav_path)[0] + "_spectrogram.png"
    if generate_spectrogram(wav_path, png_path):
        print(f"スペクトログラム: {png_path}")
    else:
        print("(sox が見つからないためスペクトログラムはスキップ)")


if __name__ == "__main__":
    main()
