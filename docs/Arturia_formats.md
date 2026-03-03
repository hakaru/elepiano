# Arturia サンプルライブラリ フォーマット解析ノート

**解析日:** 2026-03-02
**対象:** /Library/Arturia/Samples/ 以下の全ライブラリ

---

## 概要 — elepiano 利用可能性マトリクス

| ライブラリ | ファイル数 | フォーマット | サンプルレート | 長さ | ゾーン数 | 利用可否 |
|---|---|---|---|---|---|---|
| **CMI V** | 1,083 WAV | float32 PCM | 14,080 Hz | ~1.16 s | 1–3 / 楽器 | △ 短すぎ |
| **Synclavier V** | 224 WAV | 16-bit PCM | 44k–100k Hz | 1–15 s | 1–2 / 楽器 | △ ゾーン不足 |
| **Emulator II V** | 1,104 .eiiwav | µ-law 8-bit | ~27,700 Hz | 2.2–2.65 s | 6 (Grand Piano) | **◎ 抽出可** |
| Augmented STRINGS | 1,852 .arta + 141 .sfz | .arta 暗号化 | — | — | — | ✗ 復号不可 |
| Mellotron V | .mrk (ZIP+AES) | AES-128-CBC | — | — | — | ✗ 暗号化 |

---

## 1. CMI V (Fairlight CMI サンプル)

### ファイルパス

```
/Library/Arturia/Samples/CMI V/Factory/
    PIANO/          (13 files: PIANOL1, PIANOM1, PIANOH1, HONKY1 etc.)
    KEYBOARD1/      (26 files: ORGAN1-9, ELEKORG, HARPSIM1 etc.)
    KEYBOARD2/      (28 files: WURLI, ELEKEYB1/2, SPINET1, LESLIE1 etc.)
    HISTRING/       (17 files: HISTRNG1-7, QUARTET, SOLOSTR1/2 etc.)
    LOSTRING/       (25 files: CELLO1-3, LOWSTR1-7, BIGSTRN1/2 etc.)
    STRINGS3/       (13 files: ORCH1-13)
```

### WAV フォーマット

```
フォーマット:     IEEE float32 PCM (WAV format type 3)
サンプルレート:   14,080 Hz
チャンネル:       1 (モノ)
ビット深度:       32-bit float
サンプル数:       16,384 (固定)
長さ:             16384 / 14080 = 1.163636 s
```

### Python での読み取り方法

```python
import struct, numpy as np

def read_cmi_wav(path):
    """float32 WAV の読み取り (wave モジュール非対応のため手動パース)"""
    with open(path, 'rb') as f:
        raw = f.read()

    fmt_idx    = raw.index(b'fmt ')
    sample_rate = struct.unpack_from('<I', raw, fmt_idx + 12)[0]  # 14080
    data_idx   = raw.index(b'data')
    data_size  = struct.unpack_from('<I', raw, data_idx + 4)[0]
    data_start = data_idx + 8

    samples = np.frombuffer(raw[data_start:data_start+data_size], dtype='<f4')
    return sample_rate, samples
```

### ピッチ対応表（主要ファイル）

| ファイル | 検出ピッチ | MIDI | 備考 |
|---|---|---|---|
| PIANOL1.wav | A3 | 57 | ピアノ低音 |
| PIANOM1.wav | A3 | 57 | ピアノ中音 |
| PIANOH1.wav | A4 | 69 | ピアノ高音 |
| ELEKORG.wav | A3 | 57 | エレクトリックオルガン |
| ORGAN1.wav | E6 | 88 | パイプオルガン |
| WURLI.wav | G#3 | 56 | ウーリッツァー |
| LESLIE1.wav | A3 | 57 | レスリー付きオルガン |
| CELESTE.wav | A5 | 81 | チェレスタ |
| HISTRNG1.wav | A4 | 69 | 高弦 |
| LOWSTR1.wav | A2 | 45 | 低弦 |

### elepiano 利用上の制約

- **長さ不足**: 1.16 s は低音域でのピッチシフト後（速度半分）で 2.33 s → 許容範囲ギリギリ
- **ゾーン少**: ピアノは A3/A4 の 2 ゾーンのみ → 全鍵盤で重いピッチシフト
- **ループ非対応**: elepiano エンジンはループ再生なし → サンプル末尾で途切れる
- **用途推奨**: 単音のエキゾチックな音色効果 (EKG, ORGAN 系) として使用

---

## 2. Synclavier V

### ファイルパス

```
/Library/Arturia/Samples/Synclavier V/
    Factory/    (127 ファイル: 管弦楽・パーカッション・電子楽器の 1-shot)
    Arturia/    (57 ファイル: アルトゥリア制作の新録サンプル)
```

### WAV フォーマット

```
フォーマット:     16-bit PCM (標準 WAV)
サンプルレート:   44,100 / 48,000 / 50,000 / 65,000 / 96,000 / 100,000 Hz (ファイルによる)
チャンネル:       1 (モノ) or 2 (ステレオ)
※ オリジナル Synclavier の録音を保持しているため変則的なサンプルレート
```

### 鍵盤系ファイル一覧

| ファイル | 検出ピッチ | sr (Hz) | 長さ | 備考 |
|---|---|---|---|---|
| Electric Piano - F.wav | A4 (69) | 50,000 | 2.00 s | EP 弱め |
| Electric Piano - FF.wav | G4 (67) | 50,000 | 5.47 s | EP 強め |
| Digital synth electric piano.wav | E4 (64) | 50,000 | 3.70 s | デジタルEP |
| Digi CMI Grand.wav | D2 (38) | 96,000 | 9.28 s | CMI 録音グランドピアノ |
| Concert Grand Piano - Forte.wav | G2 (43) | 50,000 | 3.80 s | コンサートグランド |
| Piano Low.wav | A#2 (46) | 44,100 | 5.79 s | グランドピアノ低音 |
| Piano - loud.wav | A#5 (82) | 50,000 | 3.10 s | グランドピアノ高音 |
| Celesta.wav | G4 (67) | 50,000 | 4.01 s | チェレスタ |
| Vibraphone - Loud.wav | C6 (84) | 44,100 | 2.17 s | ビブラフォン |
| Vibraphone - Soft.wav | A4 (69) | 44,100 | 2.86 s | ビブラフォン |

### elepiano 利用上の制約

- 楽器ごとのゾーン数が 1–2 のみ → 全鍵盤をカバーする多ゾーン構成なし
- ピアノは 2–3 ゾーンだが、元々はデモンストレーション用の 1-shot
- **用途推奨**: 単音・アクセント・テクスチャ用途。フルプレイアブルな音源としては不十分

---

## 3. Emulator II V ← 最有望

### ファイルパス

```
/Library/Arturia/Samples/Emulator II V/Factory/
    04 Grand Piano/     (6 ファイル: A2, F#3, D4, A#4, F#5, D6) ← 最重要
    05 Marcato Strings/ (10 ファイル: C2, C3, C6, E1, E4, E5, G2, G3, G4, G0)
    06 Bass, Synth, Drums/ (22 ファイル)
    07 Percussion #1/   (34 ファイル)
    08 Cello & Violin/  (26 ファイル)
    09 Orchestra Tune/  (1 ファイル: A4, 15.55 s)
    10 Stacked Strings/ (5 ファイル)
    11 Acoustic Guitar/ (6 ファイル)
    ...
    Factory/            (124 WAV + その他: 606/707/808 ドラムマシン音)
```

### .eiiwav フォーマット

```
オフセット  サイズ  内容
0           4       バージョン = 0x00000100 (= 256 = v1.0)
4           4       total_remaining = file_size - 20
8           4       loop_end   (サンプル単位。= 4 の場合はループなし)
12          4       loop_start (サンプル単位。= file_size - 28 の場合はループなし)
16          12      (不明フィールド)
28          N       µ-law 8-bit 音声データ (N = file_size - 28)
```

**ループ情報の解釈:**

- `loop_start < loop_end` かつ `loop_end < file_size - 28` → ループあり
  - アタック部: samples[0 .. loop_start-1]
  - ループ部: samples[loop_start .. loop_end-1]（繰り返し再生）
  - リリース部: samples[loop_end .. EOF]（note off 後）
- `loop_start = file_size - 28` → ループなし（Piano D6 等）

### µ-law デコード

```python
import numpy as np
import struct

def read_eiiwav(path):
    """Returns (samples_float32, loop_start, loop_end, sample_rate)"""
    SAMPLE_RATE = 27700  # Emulator II 実機の標準クロック由来

    with open(path, 'rb') as f:
        data = f.read()

    loop_end   = struct.unpack_from('<I', data, 8)[0]
    loop_start = struct.unpack_from('<I', data, 12)[0]
    body       = np.frombuffer(data[28:], dtype=np.uint8)

    # G.711 µ-law decode (vectorized)
    b      = (~body).astype(np.int32) & 0xff
    sign   = b & 0x80
    exp    = (b >> 4) & 0x07
    mant   = b & 0x0f
    linear = (mant | 0x10) << (exp + 3)
    linear -= 0x84
    linear = np.where(sign != 0, -linear, linear)
    samples = linear.astype(np.float32) / 32768.0

    return samples, loop_start, loop_end, SAMPLE_RATE

def eiiwav_to_flac(in_path, out_path, midi_note):
    """Convert .eiiwav to FLAC at 44100 Hz via ffmpeg."""
    import subprocess, tempfile, os

    samples, loop_start, loop_end, sr = read_eiiwav(in_path)

    # µ-law → int16 raw PCM
    raw_pcm = (samples * 32767).clip(-32768, 32767).astype(np.int16).tobytes()

    with tempfile.NamedTemporaryFile(suffix='.raw', delete=False) as tmp:
        tmp.write(raw_pcm)
        tmp_path = tmp.name

    try:
        subprocess.run([
            'ffmpeg', '-y',
            '-f', 's16le', '-ar', str(sr), '-ac', '1',
            '-i', tmp_path,
            '-ar', '44100',  # resample to 44100 Hz
            out_path
        ], check=True, capture_output=True)
    finally:
        os.unlink(tmp_path)
```

### Grand Piano ゾーンマップ

```
MIDI  21– 49 → piano A2.eiiwav   (root = A2, MIDI 45)
MIDI  50– 58 → piano F#3.eiiwav  (root = F#3, MIDI 54)
MIDI  59– 66 → piano D4.eiiwav   (root = D4, MIDI 62)
MIDI  67– 74 → piano A#4.eiiwav  (root = A#4, MIDI 70)
MIDI  75– 82 → piano F#5.eiiwav  (root = F#5, MIDI 78)
MIDI  83–108 → Piano D6.eiiwav   (root = D6, MIDI 86)
```

境界は隣接サンプルのルートノートの中点で決定。

### ファイル名と MIDI ノートの対応

ファイル名に音名が含まれる (`piano A2` → A2 = MIDI 45)。
音名→MIDI変換: C4=60 基準、`#` = シャープ、オクターブ数値はそのまま。

```python
def note_name_to_midi(name_str):
    """例: 'A2' → 45, 'F#3' → 54, 'A#4' → 70, 'D6' → 86"""
    NOTE_MAP = {'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}
    if '#' in name_str:
        note, octave = name_str[0], int(name_str[2:])
        semitone = NOTE_MAP[note] + 1
    else:
        note, octave = name_str[0], int(name_str[1:])
        semitone = NOTE_MAP[note]
    return (octave + 1) * 12 + semitone
```

---

## 4. Augmented STRINGS / VOICES

### ファイル構成

```
/Library/Arturia/Samples/Augmented STRINGS/Factory/
    SFZ/        (141 .sfz ファイル — オープン形式)
    *.arta      (1,852 ファイル — Arturia 独自暗号化)
```

### SFZ フォーマット

SFZ はオープンな XML 風テキスト形式でゾーンマッピングを記述する。

```sfz
<region> sample=STR_Chocolate_PSS002_A#1.arta pitch_keycenter=A#2 lokey=A2 hikey=B2
```

ただし `sample=` が指す `.arta` ファイルが暗号化されているため、音声データの抽出は不可能。

### .arta 暗号化

- エントロピー: 約 8.0 bits/byte（AES-128-CBC と推定）
- 鍵: Arturia プラグインが動的に取得（公開なし）
- **結論**: 抽出不可

---

## 5. Mellotron V (.mrk)

NKX 同様に AES-128-CBC 暗号化。詳細は `NKX_format.md` を参照。
**代替手段**: Logic Pro の `Mellotron_consolidated.caf` を使用（→ `docs/Logic_Mellotron_extraction.md`）

---

## 参考: Arturia サンプルディレクトリ一覧

```
/Library/Arturia/Samples/
├── Augmented STRINGS/      1.10 GB  (.arta 暗号化)
├── Augmented VOICES/        390 MB  (.arta 暗号化)
├── CMI V/                   323 MB  (WAV float32 直接利用可)
├── Emulator II V/            93 MB  (.eiiwav µ-law 要デコード)
├── Mellotron V/             --  MB  (.mrk AES暗号化)
└── Synclavier V/            105 MB  (WAV 直接利用可)
```
