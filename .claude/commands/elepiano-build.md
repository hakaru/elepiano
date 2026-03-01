# elepiano C++ エンジン ビルド & 実行

elepiano シンセエンジンを Raspberry Pi 上でビルド・起動する手順。
attack / release サンプルの両方に対応。

---

## アーキテクチャ概要

```
main.cpp
├── SampleDB  (attack_db)    ← attack samples.json をロード
├── SampleDB  (release_db)   ← release samples.json をロード（省略可能）
├── SynthEngine              ← MIDI イベント処理 / Voice スケジューリング
│   ├── Voice × 32          ← attack / release 各1 voice（同時発音）
│   │   ├── is_release_voice=false → note_off() で 200ms fade
│   │   └── is_release_voice=true  → note_off() は no-op（末尾まで再生）
│   └── SampleDB::find(note, vel)  ← O(log n) 最近傍検索
├── AudioOutput              ← ALSA 出力（stereo float, 44100Hz）
└── MidiInput                ← ALSA シーケンサ入力
```

### Voice ライフサイクル

```
[NOTE ON]  → attack voice: IDLE → PLAYING
[NOTE OFF] → attack voice: PLAYING → RELEASING (200ms fade)
           → release voice が起動: IDLE → PLAYING → (末尾到達) → IDLE
```

---

## 必要なパッケージ（Pi 上）

```bash
sudo apt update
sudo apt install -y \
    libasound2-dev \
    libflac-dev \
    nlohmann-json3-dev \
    cmake \
    build-essential
```

---

## ビルド手順

```bash
cd ~/elepiano          # Pi 上のプロジェクトルート

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

クロスコンパイル（Mac → Pi）の場合:

```bash
cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE=cmake/pi-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# → build/elepiano を Pi に scp で転送
```

---

## サンプルデータ転送（Mac → Pi）

```bash
# samples.json のみ転送（audio/ は大容量なので別途）
rsync -av --include="samples.json" --exclude="audio/**" \
    data/ pi@raspberrypi:~/elepiano/data/

# audio/ FLAC ファイルも転送（初回のみ、約 4.6GB + release 分）
rsync -av data/rhodes-classic/audio/     pi@raspberrypi:~/elepiano/data/rhodes-classic/audio/
rsync -av data/rhodes-classic-relf/audio/ pi@raspberrypi:~/elepiano/data/rhodes-classic-relf/audio/
```

---

## 起動コマンド

### Attack のみ（旧動作・後方互換）

```bash
./build/elepiano \
    data/rhodes-classic/samples.json
```

### Attack + Release Fade（推奨）

```bash
./build/elepiano \
    data/rhodes-classic/samples.json \
    data/rhodes-classic-relf/samples.json
```

### 引数仕様

```
./elepiano [attack_json] [release_json] [alsa_device]
  attack_json   : attack samples.json のパス（デフォルト: data/rhodes-classic/samples.json）
  release_json  : release samples.json のパス（省略可 → release サンプルなし）
  alsa_device   : ALSA デバイス名（デフォルト: hw:pisound）
```

### 他の音色

```bash
# Wurlitzer 200A
./build/elepiano data/wurlitzer-200a/samples.json

# Vintage Vibe EP
./build/elepiano data/vintage-vibe-ep/samples.json

# Rhodes + Mechanical Release
./build/elepiano \
    data/rhodes-classic/samples.json \
    data/rhodes-classic-mchrel/samples.json
```

---

## 利用可能なサンプルデータ一覧

| ディレクトリ | 種別 | ファイル数 | 用途 |
|---|---|---:|---|
| `data/rhodes-classic/` | CLR attack r10 | 1,615 | attack (推奨) |
| `data/rhodes-classic-relf/` | CLR RelF | 1,760 | release fade ★ |
| `data/rhodes-classic-relm/` | CLRRelMr3 | 1,760 | release mechanical |
| `data/rhodes-classic-mchrel/` | CLRMchRel | 1,056 | mechanical release |
| `data/rhodes-classic-mchr03/` | CLRMchr03 | 1,408 | mechanical attack r03 |
| `data/wurlitzer-200a/` | NMWurl attack | 1,024 | attack |
| `data/vintage-vibe-ep/` | VVEP attack | 1,539 | attack |
| `data/rhodes/` | lacrmsp pattern1 | 38 | 旧テスト用 |

★ 通常の鍵盤リリース音。最初は `rhodes-classic-relf` を推奨。

---

## サンプル抽出（未実行の場合）

```bash
DB_RHODES="/Volumes/Media/AudioLibraries/Spectrasonics/STEAM/Keyscape/Soundsources/Factory/Keyscape Library/Keyboards/Rhodes - Classic.db"

# Attack
python3 tools/extract_samples.py "$DB_RHODES" data/rhodes-classic     rhodes-attack

# Release Fade（key-up release）
python3 tools/extract_samples.py "$DB_RHODES" data/rhodes-classic-relf   rhodes-relf

# Release Mechanical
python3 tools/extract_samples.py "$DB_RHODES" data/rhodes-classic-relm   rhodes-relm

# Mechanical Release
python3 tools/extract_samples.py "$DB_RHODES" data/rhodes-classic-mchrel rhodes-mchrel

# Mechanical Attack r03
python3 tools/extract_samples.py "$DB_RHODES" data/rhodes-classic-mchr03 rhodes-mchr03
```

詳細は `/keyscape-extract` を参照。

---

## 主要ソースファイル

| ファイル | 役割 |
|---|---|
| `src/main.cpp` | エントリーポイント。argv で attack/release json を受け取る |
| `src/synth_engine.cpp` | NOTE ON/OFF ハンドリング、voice スケジューリング |
| `src/voice.cpp` | 1音の再生（linear interpolation, release fade） |
| `src/sample_db.cpp` | samples.json ロード、note/vel 検索 O(log n) |
| `src/flac_decoder.cpp` | libFLAC ラッパー（CRC エラー無視設定済み） |
| `src/audio_output.cpp` | ALSA 出力（period=256, periods=4, 23ms レイテンシ） |
| `src/midi_input.cpp` | ALSA シーケンサ MIDI 受信 |

---

## トラブルシューティング

### `libFLAC が見つかりません`

```bash
sudo apt install libflac-dev
```

### `samples.json が開けません`

カレントディレクトリを確認。`build/` でなくプロジェクトルートから実行:

```bash
cd ~/elepiano
./build/elepiano data/rhodes-classic/samples.json
```

### ALSA アンダーラン頻発

`CMakeLists.txt` の `acfg.period_size` / `acfg.periods` を調整:

```cpp
acfg.period_size = 512;  // デフォルト 256
acfg.periods     = 4;
```

### voice が足りない（32音以上の高速アルペジオ）

`src/synth_engine.hpp` の `MAX_VOICES` を増やす（現在 32）。
ただし Pi の CPU 負荷に注意。
