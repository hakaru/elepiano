# BLE-MIDI iPhone コントロールアプリ計画

作成日: 2026-03-04

## アーキテクチャ

```
iPhone (SwiftUI + MIDIKit)
  ↓ BLE-MIDI (Apple MIDI GATT Service)
Pi5 (bluetoothd + btmidi-server --enable-midi)
  ↓ ALSA Sequencer 仮想ポート（BlueZ 自動生成）
elepiano MidiInput（既存 ALSA Sequencer client: 変更不要）
```

## Phase 1: Pi5 BLE-MIDI セットアップ（要物理アクセス）

- [ ] P1-1: BlueZ を --enable-midi 付きでソースビルド
- [ ] P1-2: bluetoothd を experimental モードで起動
- [ ] P1-3: btmidi-server をシステムサービス化
- [ ] P1-4: BLE ペアリングと ALSA ポート自動接続の確認
- [ ] P1-5: elepiano 起動スクリプトに aconnect 自動接続追加

## Phase 2: iOS アプリ基盤

ディレクトリ: /Users/hakaru/DEVELOP/elepiano/iOS/ElepianoControl/

- [ ] P2-1: XcodeGen project.yml 作成
- [ ] P2-2: Info.plist (NSBluetoothAlwaysUsageDescription)
- [ ] P2-3: MIDIManager セットアップ (AppState)
- [ ] P2-4: BLE-MIDI デバイス検索・接続 UI

## Phase 3: iOS アプリ UI

- [ ] P3-1: CC スライダーコンポーネント
- [ ] P3-2: FxChain コントロールパネル (CC 70-79)
- [ ] P3-3: オルガン Drawbars パネル (CC 12-20, CC 64)
- [ ] P3-4: モード切替 (Piano/Organ) タブ
- [ ] P3-5: MIDI 送信ロジック

## Phase 4: プリセット機能

- [ ] P4-1: PresetStore (Codable + UserDefaults)
- [ ] P4-2: PresetView (一覧 + 保存/読み込み)

## CC マッピング

### FxChain (ピアノモード)
| CC | パラメータ | 範囲 |
|----|-----------|------|
| 70 | Drive | 1.0〜8.0 |
| 71 | Lo EQ | -12〜+12 dB |
| 72 | Hi EQ | -12〜+12 dB |
| 73 | Tremolo Depth | 0〜0.8 |
| 74 | Tremolo Rate | 0.5〜8 Hz |
| 75 | Delay Time | 0〜0.5s |
| 76 | Delay Feedback | 0〜0.85 |
| 77 | Chorus Rate | 0.5〜2 Hz |
| 78 | Chorus Depth | 0〜20 ms |
| 79 | Chorus Wet | 0〜1.0 |

### オルガンモード
| CC | パラメータ |
|----|-----------|
| 12-20 | Drawbar 0-8 (チャンネルごと) |
| 64 | Leslie Fast/Slow |
| 7 | Volume (マニュアルごと) |
| 1 | V/C モード |
