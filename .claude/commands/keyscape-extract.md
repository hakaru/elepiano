# Keyscape サンプル抽出

`docs/keyscape-sample-extraction.md` のガイドに従って Keyscape .db ファイルからサンプルを抽出する。

---

## 前提・注意

- **Keyscape のライセンスを所持していること**が前提
- 抽出したサンプルは個人使用の範囲に限ること
- 再配布・商用利用は Spectrasonics ライセンス条項に従うこと

---

## ファイル構成

```
/Volumes/Media/AudioLibraries/Spectrasonics/STEAM/Keyscape/
└── Soundsources/Factory/Keyscape Library/Keyboards/
    ├── Rhodes - Classic.db
    ├── Wurlitzer 140B.db          ← Wurlitzer 140B (4096ファイル)
    ├── Wurlitzer 200A.db          ← Wurlitzer 200A (1967ファイル) ✅ 抽出確認済み
    ├── CP-80 Grand.db
    └── ...
```

各 `.db` ファイルが1音色に相当し、全サンプルを内包している。

---

## .db ファイル構造

```
┌─────────────────────────────────────────────┐
│  XML ヘッダー                                │
│  <FileSystem>                               │
│    <FILE name="RR01 lacrmsp 25 60.wav"      │
│          offset="0" size="12345"/>          │
│    <FILE name="..." offset="12345" .../>    │
│    ...                                      │
│  </FileSystem>                              │
│  \n  (0x0A, 1バイト)                        │
├─────────────────────────────────────────────┤
│  データセクション                            │
│  [SpCA データ #0]  ← offset=0 から          │
│  [SpCA データ #1]  ← offset=12345 から      │
│  ...                                        │
└─────────────────────────────────────────────┘
```

**重要:** データセクション開始位置 = `</FileSystem>` の直後の `\n` を**スキップした**位置。
`\n` を含めたままオフセットを計算するとバイナリが1バイトずれる。

---

## SpCA フォーマット

### マジックバイト置換

SpCA ファイルは FLAC をラップした独自形式。先頭 4 バイトを置換するだけで FLAC として扱える。

```
SpCA データ: [53 70 43 41] [FLACメタデータ] [FLACオーディオフレーム]
              ↑ "SpCA"

置換後:      [66 4C 61 43] [FLACメタデータ] [FLACオーディオフレーム]
              ↑ "fLaC"
```

### サンプル種別と暗号化

| 種別 | ファイル名パターン | 暗号化 | 対象 .db |
|------|-----------------|--------|---------|
| **Pattern 1** | `RR01 lacrmsp VV NN.wav` | なし（magic 置換のみ） | Rhodes |
| **Rhodes Attack** | `RR01_SL01 CLR r10_NN v##.wav` | なし（magic 置換のみ） | Rhodes |
| **Wurlitzer 200A** | `NMWurl NN a VMIN-VMAX-o.wav` | なし（magic 置換のみ） | Wurlitzer 200A |
| **Vintage Vibe EP** | `RR01_SL01 VVEP r06_NN v##[+-]?.wav` | なし（magic 置換のみ） | Vintage Vibe EP |

### XOR 暗号化アルゴリズム（Sustain のみ）

```
XOR キー: [0xEF, 0x42, 0x12, 0xBC]  (4バイト周期)
```

- **フレーム0**: 暗号化なし（ただし CRC-16 が意図的に破損している）
- **フレーム1以降**: 全バイトをキーの 4 通りローテーションで XOR

```python
# ローテーションの選択:
# フレーム1先頭の 2 バイトが 0xFF 0xF8/F9 になるローテーションを選ぶ
for rot in range(4):
    key = BASE_KEY[rot:] + BASE_KEY[:rot]
    if (data[pos] ^ key[0]) == 0xFF and (data[pos+1] ^ key[1]) & 0xFE == 0xF8:
        # これが正しいローテーション

# 適用:
key = BASE_KEY[rot:] + BASE_KEY[:rot]
decrypted = bytes(b ^ key[i % 4] for i, b in enumerate(segment))
```

---

## ファイル名の意味

### Pattern 1

```
RR01 lacrmsp 25 60.wav
│    │       │  └─ MIDI ノート番号 (60 = C4)
│    │       └─── 速度インデックス (25)
│    └─────────── サンプル種別識別子 (lacrmsp)
└────────────────── ラウンドロビン番号 (01)
```

### Sustain

```
RR01_SL01 CLR r10_60 v25.wav
│    │    │   │  │   └─ 速度インデックス (25)
│    │    │   │  └───── MIDI ノート番号 (60 = C4)
│    │    │   └──────── レイヤー識別子 (r10)
│    │    └──────────── アーティキュレーション (CLR = clear)
│    └───────────────── サステインレイヤー番号 (SL01)
└────────────────────── ラウンドロビン番号 (01)
```

### Wurlitzer 200A（Attack）

```
NMWurl 60 a 50-64-o.wav
│      │  │ │  │  └─ サフィックス (o = original / attack)
│      │  │ │  └──── 速度上限 (MIDI vel 64)
│      │  │ └─────── 速度下限 (MIDI vel 50)
│      │  └───────── レイヤー識別子 (a)
│      └──────────── MIDI ノート番号 (60 = C4)
└─────────────────── 識別子 (NMWurl = Nick Milo Wurlitzer)
```

- Release サンプルは `NMWurl 60 a 50-64 Rls-o.wav`（`Rls` 付き）—現在は抽出対象外

---

## サンプル規模

| 種別 | 対象 | ノート数 | 速度レイヤー | 総ファイル数 | 状態 |
|------|------|---------|------------|------------|------|
| Pattern 1 | Rhodes | 19 | 2 | **38** | ✅ 確認済み |
| Rhodes Attack | Rhodes | 85 | 19 | **1615** | ✅ 確認済み |
| Wurlitzer 200A | Wurlitzer 200A | 64 | 16 | **1024** | ✅ 確認済み |
| Vintage Vibe EP | Vintage Vibe EP | 81 | 19×3RR | **1539** | ✅ 確認済み |

---

## 抽出ツール使い方

### 必要なもの

```bash
# Python 3.10+
python3 --version

# ffmpeg (WAV 変換に使用)
brew install ffmpeg    # macOS
apt install ffmpeg     # Linux
```

### Pattern 1 の抽出（推奨・動作確認済み）

```bash
cd /Volumes/Dev/DEVELOP/elepiano

python3 tools/extract_samples.py \
    "/Volumes/Media/AudioLibraries/Spectrasonics/STEAM/Keyscape/Soundsources/Factory/Keyscape Library/Keyboards/Rhodes - Classic.db" \
    data/rhodes \
    pattern1
```

**出力:**
```
data/rhodes/
├── samples.json              ← MIDI ノート/速度マッピング
└── audio/
    ├── rr1_vel025_n024.flac  ← ノート24, vel_idx=25
    ├── rr1_vel026_n024.flac  ← ノート24, vel_idx=26
    ├── rr1_vel025_n035.flac
    └── ...  (計38本)
```

### Rhodes Attack の抽出（CLR attack 1615ファイル）

```bash
python3 tools/extract_samples.py \
    "/Volumes/Media/AudioLibraries/Spectrasonics/STEAM/Keyscape/Soundsources/Factory/Keyscape Library/Keyboards/Rhodes - Classic.db" \
    data/rhodes-classic \
    rhodes-attack
```

**出力:**
```
data/rhodes-classic/
├── samples.json                   ← MIDI ノート/速度マッピング
└── audio/
    ├── rr1_vel001_n021.flac       ← ノート21 (A0), vel_idx=1
    ├── rr1_vel002_n021.flac
    ├── ...
    └── rr1_vel019_n108.flac       ← ノート108 (C8), vel_idx=19
    (計 1615 本)
```

- 85ノート(21-108) × 19速度レイヤー = 1615エントリ
- XOR 暗号化なし、magic 置換のみ

### Wurlitzer 200A の抽出（動作確認済み）

```bash
python3 tools/extract_samples.py \
    "/Volumes/Media/AudioLibraries/Spectrasonics/STEAM/Keyscape/Soundsources/Factory/Keyscape Library/Keyboards/Wurlitzer 200A.db" \
    data/wurlitzer-200a \
    wurl200a
```

**出力:**
```
data/wurlitzer-200a/
├── samples.json                   ← MIDI ノート/速度マッピング
└── audio/
    ├── rr1_vel000_n033.flac       ← ノート33 (A1), vel 0-14
    ├── rr1_vel015_n033.flac       ← ノート33, vel 15-24
    ├── ...
    └── rr1_vel126_n096.flac       ← ノート96 (C7), vel 126-127
    (計 1024 本)
```

- Attack サンプルのみ抽出（Release = `Rls-o.wav` は除外）
- XOR 暗号化なし、magic 置換のみ

### samples.json の形式

```json
{
  "instrument": "rhodes-classic",
  "sample_rate": 44100,
  "bit_depth": 24,
  "pattern": "pattern1",
  "samples": [
    {
      "midi_note": 60,
      "velocity_min": 0,
      "velocity_max": 63,
      "velocity_idx": 25,
      "round_robin": 1,
      "file": "audio/rr1_vel025_n060.flac"
    },
    ...
  ]
}
```

---

## 動作確認済み環境

| 項目 | 値 |
|------|---|
| Keyscape バージョン | 2.x (STEAM) |
| Python | 3.11 |
| OS | macOS 15 (Sequoia) |
| 出力フォーマット | FLAC 44100Hz / 24-bit / モノ |
| Rhodes Pattern 1 | 38 / 38 ✅ |
| Rhodes Attack | 1615 / 1615 ✅ |
| Wurlitzer 200A | 1024 / 1024 ✅ |
| Vintage Vibe EP | 1539 / 1539 ✅ |

> `flac` / `ffmpeg` コマンドで LOST_SYNC が出るのは SpCA 仕様上の CRC 不整合によるもので正常。C++ の `error_cb` が LOST_SYNC/BAD_HEADER を意図的に無視しているため Pi 上の合成エンジンでは問題なく動作する。

---

## トラブルシューティング

### `SpCA マジックバイトが見つかりません`

XML ヘッダーのオフセット計算がずれている可能性。
`</FileSystem>` 直後の `\n` (0x0A) をスキップしているか確認。

```python
while xml_end < len(data) and data[xml_end] in (0x0A, 0x0D, 0x20):
    xml_end += 1
```

### `正しい XOR ローテーションが見つかりません`

Sustain サンプルで `_find_encrypted_frame1()` の探索範囲（デフォルト 200,000 バイト）内にフレーム1が見つからない場合。
→ `max_scan` パラメーターを増やして再試行。

### FLAC デコードが空になる

libFLAC が CRC エラーで全フレームをスキップしている可能性。
`snd-usb-audio` の `MD5_CHECKING` を `false` に設定（`decode_flac_file()` 内で実装済み）。

---

## 内部実装リファレンス

| 処理 | ファイル | 関数 |
|------|---------|------|
| .db XML パース | `tools/extract_samples.py` | `parse_db_header()` |
| SpCA → FLAC 変換 | `tools/extract_samples.py` | `decode_spca()` |
| 暗号化フレーム探索 | `tools/extract_samples.py` | `_find_encrypted_frame1()` |
| FLAC → PCM デコード | `src/flac_decoder.cpp` | `decode_flac_file()` |
| サンプル検索 (O(log n)) | `src/sample_db.cpp` | `SampleDB::find()` |
