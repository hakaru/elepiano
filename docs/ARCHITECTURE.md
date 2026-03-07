# elepiano システムアーキテクチャ

最終更新: 2026-03-07

---

## システム全体図

```
┌─────────────────────────────────────────────────────────┐
│                     iOS アプリ                          │
│  LandscapeCCView (Mix / Tone / Space / Organ ページ)    │
│  BLEManager (CoreBluetooth) → sendCC / sendPC / sendCmd │
└──────────────────────┬──────────────────────────────────┘
                       │ BLE GATT (Bluetooth LE)
                       │ Service UUID: e1e00000-...
                       ▼
┌─────────────────────────────────────────────────────────┐
│              Raspberry Pi 5 (Pi側)                      │
│                                                         │
│  ble_bridge.py (Python, dbus-next)                      │
│  ├── GattApplication (BlueZ GATT サービス登録)          │
│  ├── StatusMonitor (UNIX socket /tmp/elepiano-status)   │
│  └── AlsaMidiSender (ALSA seq 直接アドレッシング)       │
│                │                                        │
│                │ ALSA MIDI (snd_seq)                    │
│                ▼                                        │
│  ┌─────────────────────────────────────────────────┐   │
│  │               elepiano (C++, メインプロセス)      │   │
│  │                                                 │   │
│  │  MidiInput ──→ SynthEngine ──┐                  │   │
│  │  (ALSA seq)    (ピアノ音源)   │                  │   │
│  │                              ├──→ FxChain ──→ AudioOutput │
│  │               SetBfreeEngine─┘   (エフェクト)  (ALSA PCM) │
│  │               (Hammond B3)                     │   │
│  │                              │                 │   │
│  │  StatusReporter ◄────────────┘                 │   │
│  │  (UNIX socket)                                 │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## コンポーネント詳細

### iOS アプリ (`ios/elepiano-remote/`)

| ファイル | 役割 |
|---------|------|
| `ElepianoRemoteApp.swift` | アプリエントリーポイント。BLEManager を EnvironmentObject として注入 |
| `BLEManager.swift` | CoreBluetooth 管理。BLE スキャン・接続・自動再接続（5秒後リトライ）|
| `LandscapeCCView.swift` | メインUI。横向き固定。サイドバー（音色）＋ページタブ（Mix/Tone/Space/Organ）|
| `CCParameter.swift` | CC パラメータ定義（番号・名前・範囲・グループ・コントロール種別）|
| `SynthStatus.swift` | Pi からの JSON ステータスモデル（program / voices / underruns / elepiano_connected）|
| `BLEConstants.swift` | BLE UUID 定数 |
| `Views/FaderView.swift` | 縦フェーダーコントロール（`.highPriorityGesture`）|
| `Views/StatusView.swift` | ステータス表示ビュー |
| `Views/ConnectionView.swift` | 接続前のスキャン表示画面 |

**BLE スロットリング:** 同じ CC を 25ms 以内に重複送信しない（最大 40Hz）。

---

### BLE Bridge (`ble/`)

| ファイル | 役割 |
|---------|------|
| `ble_bridge.py` | メインループ。BLE ↔ ALSA MIDI 変換 |
| `gatt_service.py` | BlueZ D-Bus GATT サービス定義 |
| `status_monitor.py` | elepiano UNIX socket からステータスを受信して BLE で通知 |
| `ble-bridge.service` | systemd サービス定義 |
| `requirements.txt` | Python 依存: `dbus-next>=0.2.3` |

**GATT キャラクタリスティック一覧:**

| UUID (e1e000xx-...) | 名前 | フラグ | フォーマット |
|--------------------|------|--------|-------------|
| e1e00001 | Status | read, notify | JSON UTF-8（最大512B）|
| e1e00002 | CC Control | write-without-response | [cc, val] 2B |
| e1e00003 | Program Change | read, write, notify | [pg] 1B |
| e1e00005 | Batch CC | write-without-response | [cc1,v1,...ccN,vN] 最大 10 ペア |
| e1e00006 | Command | write | UTF-8 コマンド文字列 |

**コマンド文字列の例:**
- `restart_elepiano` — elepiano プロセスを systemd 経由で再起動
- `restart_ble_bridge` — BLE bridge 自身を再起動

**自動再接続機構:**
- AlsaMidiSender が 10 秒ごとに elepiano クライアント ID を確認し、変化があれば再接続
- ble-bridge.service は `After=elepiano.service` で elepiano 後に起動

---

### elepiano メインプロセス (`src/`)

#### スレッド構成

```
メインスレッド
├── StatusReporter::update() (100ms ポーリング)
└── MIDI ログキュー消費

MIDI スレッド (pthread, SCHED_OTHER)
└── MidiInput::run() → コールバック
    ├── SynthEngine::push_event()  (SPSC queue, 256 entries)
    ├── SetBfreeEngine::push_event() (SPSC queue, 256 entries)
    └── FxChain::set_param()       (SPSC queue, 32 entries)

オーディオスレッド (pthread, SCHED_FIFO priority=80)
└── AudioOutput::run() → コールバック
    ├── SynthEngine::mix() or SetBfreeEngine::mix()  (音源レンダリング)
    └── FxChain::process()  (エフェクト処理)
```

**スレッド間通信:** `SpscQueue<T, N>` (Lock-free Single Producer Single Consumer キュー)。
- MIDI→オーディオ: MidiEvent キュー + FxEvent キュー
- オーディオ→メイン: AudioOutput::underrun_count (atomic)

#### モジュール一覧

| ファイル | クラス | 役割 |
|---------|--------|------|
| `main.cpp` | — | エントリーポイント。モード選択（piano/organ）、スレッド起動 |
| `synth_engine.hpp/cpp` | `SynthEngine` | サンプルベース音源エンジン。最大 32 ボイス、ポリフォニック |
| `organ_engine.hpp/cpp` | `OrganEngine` | 物理モデリング トーンホイールオルガン（独自実装）|
| `setbfree_engine.hpp/cpp` | `SetBfreeEngine` | setBfree Hammond B3 エンジン C++ ラッパー |
| `fx_chain.hpp/cpp` | `FxChain` | エフェクトチェーン（詳細後述）|
| `midi_input.hpp/cpp` | `MidiInput` | ALSA seq MIDI 入力。全 HW ポートに自動接続 |
| `audio_output.hpp/cpp` | `AudioOutput` | ALSA PCM 出力。ステレオ 44100Hz、period=128 |
| `sample_db.hpp/cpp` | `SampleDB` | FLAC サンプルのロード・検索（ノート番号+ベロシティ）|
| `voice.hpp/cpp` | `Voice` | 1ボイス再生。サンプル補間・ピッチベンド・エンベロープ |
| `lv2_host.hpp/cpp` | `Lv2Host` | LV2 プラグインホスト（`ELEPIANO_ENABLE_LV2=1` 時のみ）|
| `ir_convolver.hpp/cpp` | `IrConvolver` | キャビネット IR 畳み込み（WAV ファイル読み込み）|
| `status_reporter.hpp/cpp` | `StatusReporter` | UNIX domain socket でステータス JSON を公開 |
| `spsc_queue.hpp` | `SpscQueue<T,N>` | Lock-free SPSC キュー |
| `flac_decoder.hpp/cpp` | — | FLAC デコード（dr_flac ラッパー）|

---

## シグナルフロー（ピアノモード）

```
MIDI Input (ALSA seq)
│
├─[NOTE ON/OFF]──────────────────────────────────────────────────────────────┐
│                                                                             │
├─[CC]──→ SynthEngine::_cc()                                                  │
│          CC7/102: volume_                                                   │
│          CC11/103: expression_                                              │
│          CC5: release_time_s_                                               │
│          CC64: sustain_held_                                                │
│                                                                             │
├─[CC]──→ FxChain::set_param()                                                │
│          ↓ (SPSC queue)                                                    │
│          FxChain::apply_param() (オーディオスレッド内)                       │
│                                                                             │
└─[PROGRAM CHANGE]──→ SynthEngine::_program_change()                          │
                                                                              │
                       SynthEngine::mix()                                     │
                       ┌─────────────────────────────────────────────────┐   │
                       │  Voice 0..31: SampleData 再生 + ピッチベンド     │◄──┘
                       │  マスターゲイン (CC7 × CC11) + ソフトクリップ   │
                       └────────────────────┬────────────────────────────┘
                                            │ stereo float buffer
                                            ▼
                       FxChain::process()
                       ┌──────────────────────────────────────────────┐
                       │ 1. Tremolo (AM, L/R 90°) or Phaser (4段 AP) │
                       │ 2. LV2 pre chain (任意) or Preamp + Cabinet  │
                       │ 3. IR Convolver (キャビネット IR)             │
                       │ 4. LV2 post chain (任意) or EQ + Chorus + Space │
                       └────────────────────┬─────────────────────────┘
                                            │
                                            ▼
                       AudioOutput (ALSA PCM, 44100Hz stereo, 16-bit)
```

---

## シグナルフロー（オルガンモード、setBfree）

```
MIDI Input
│
├─[NOTE ON/OFF / CC]──→ SetBfreeEngine::push_event()
│                        ↓ (SPSC queue)
│                       setBfree: tonegen + vibrato + overdrive + whirl (Leslie)
│                       → stereo float buffer (Leslie L/R)
│                                            │
│                                            ▼
│                       FxChain::process() (同上)
│                                            │
│                                            ▼
│                       AudioOutput
```

---

## ステータス通知フロー

```
elepiano (C++)
└── StatusReporter::update() (100ms)
    └── UNIX socket: /tmp/elepiano-status.sock
        └── JSON行: {"running":true,"program":1,"name":"Rhodes","voices":3,"underruns":0}

ble_bridge.py
└── StatusMonitor::run() (非同期)
    └── status_json を StatusCharacteristic に書き込み → BLE Notify

iOS BLEManager
└── didUpdateValueFor (Status UUID)
    └── JSONDecoder → SynthStatus → @Published status
        └── LandscapeCCView で表示（音色名・ボイス数・接続状態）
```

---

## 音色プログラム（デフォルト設定）

| PG # | 名前 | Attack DB | Release DB | 方式 |
|------|------|-----------|------------|------|
| 1 | Rhodes | `data/rhodes-classic/samples.json` | あり | SampleDB |
| 2 | LA Custom | `data/rhodes-la-custom/samples.json` | あり | SampleDB |
| 3 | Vintage Vibe | `data/vintage-vibe-ep/samples.json` | なし | SampleDB |
| 4 | Organ | — | — | SetBfreeEngine |

プログラムチェンジで音色切替。オルガン PG では SynthEngine をバイパスして SetBfreeEngine を使用。

---

## データフォーマット

### samples.json
```json
{
  "sample_rate": 44100,
  "samples": [
    {
      "path": "audio/note_60_v80.flac",
      "note": 60,
      "velocity_lo": 64,
      "velocity_hi": 95
    }
  ]
}
```

### SampleDB インメモリ形式
- PCM: `vector<int16_t>`（44100Hz / 24bit → int16 変換済み）
- 再生時に `int16 * (1/32767)` で float に変換

### ステータス JSON
```json
{
  "running": true,
  "program": 1,
  "name": "Rhodes",
  "voices": 3,
  "underruns": 0,
  "elepiano_connected": true
}
```

---

## 外部依存ライブラリ

| ライブラリ | 用途 | 提供方法 |
|-----------|------|---------|
| ALSA (libasound2) | MIDI 入力 + オーディオ出力 | システムパッケージ |
| nlohmann/json | samples.json パース | ヘッダーオンリー（FetchContent or apt）|
| dr_flac | FLAC デコード | ヘッダーオンリー (`src/dr_flac.h`) |
| setBfree | Hammond B3 音源 | `vendor/setBfree/` 静的リンク |
| lilv-0 (オプション) | LV2 プラグインホスト | `ELEPIANO_ENABLE_LV2=1` 時のみ |
| dbus-next | Python BLE (BlueZ) | pip |
| CoreBluetooth | iOS BLE | iOS SDK |
