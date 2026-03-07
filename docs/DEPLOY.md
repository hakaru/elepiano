# elepiano デプロイ手順

最終更新: 2026-03-07

---

## 目次

1. [Pi へのビルドとデプロイ](#pi-へのビルドとデプロイ)
2. [iOS アプリのビルドとデプロイ](#ios-アプリのビルドとデプロイ)
3. [systemd サービス設定](#systemd-サービス設定)
4. [環境変数一覧](#環境変数一覧)
5. [起動確認](#起動確認)

---

## Pi へのビルドとデプロイ

### Pi 上での直接ビルド（推奨）

Pi 5 上で直接ビルドする方法。クロスコンパイルより確実。

#### 1. 依存パッケージのインストール

```bash
sudo apt update
sudo apt install -y \
  cmake \
  build-essential \
  libasound2-dev \
  nlohmann-json3-dev
```

LV2 を有効にする場合:

```bash
sudo apt install -y liblilv-dev
```

#### 2. ソースの取得 / 更新

Mac から Pi に rsync する場合:

```bash
# Mac 側で実行
rsync -avz --exclude=build --exclude=data \
  /Volumes/Dev/DEVELOP/elepiano/ \
  hakaru@raspberrypi5.local:~/elepiano/
```

Pi 上でのみ作業する場合は git pull:

```bash
git pull origin main
```

#### 3. CMake ビルド

```bash
cd ~/elepiano
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

LV2 を有効にする場合:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DELEPIANO_ENABLE_LV2=ON
cmake --build build -j$(nproc)
```

#### 4. バイナリのデプロイ

```bash
sudo cp build/elepiano /usr/local/bin/elepiano
sudo chmod +x /usr/local/bin/elepiano
```

### Mac からのクロスコンパイル（参考）

aarch64 ツールチェーンが必要。通常は Pi 上での直接ビルドを推奨。

```bash
brew install aarch64-unknown-linux-gnu
cmake -B build-cross \
  -DCMAKE_TOOLCHAIN_FILE=cmake/pi-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-cross -j$(nproc)
```

---

## BLE Bridge のデプロイ

BLE Bridge は Python スクリプトで動作する。

### 依存パッケージのインストール

```bash
cd ~/elepiano/ble
pip3 install -r requirements.txt
# requirements.txt: dbus-next>=0.2.3
```

### Bluetooth 権限の確認

BlueZ GATT サービスには root 権限が必要。
systemd サービスは `User=root` で動作するため通常は設定不要。

開発中にテスト実行する場合:

```bash
sudo python3 ~/elepiano/ble/ble_bridge.py
```

---

## iOS アプリのビルドとデプロイ

### 必要環境

- macOS 13 以上
- Xcode 15 以上
- iPhone (iOS 16 以上)
- Apple Developer アカウント（実機デプロイ時）

### Xcode でのビルド

1. `ios/elepiano-remote/elepiano-remote.xcodeproj` を Xcode で開く
2. ターゲットデバイスを接続済みの iPhone に設定
3. Bundle ID と Signing チームを自分のアカウントに設定
4. `Cmd+R` でビルド & 実行

### プロジェクト設定確認

- Deployment Target: iOS 16.0 以上
- Orientation: Landscape（横向き固定）
- Background Modes: Bluetooth LE accessories（Info.plist 設定済み）

### BLE 接続方式

iOS アプリは起動時に自動スキャンを開始する。
Pi 側 BLE bridge が起動していれば自動接続する。

接続条件（どちらかを満たせば接続）:
- Service UUID `e1e00000-0001-4000-8000-00805f9b34fb` を通知しているデバイス
- デバイス名に `elepiano` または `hakarupiano` を含むデバイス

---

## systemd サービス設定

### elepiano.service の作成

`/etc/systemd/system/elepiano.service` を以下の内容で作成する:

```ini
[Unit]
Description=elepiano MIDI Synthesizer
After=sound.target

[Service]
Type=simple
User=root
WorkingDirectory=/home/hakaru/elepiano
ExecStart=/usr/local/bin/elepiano \
  --pg data/rhodes-classic/samples.json data/rhodes-classic-rel/samples.json \
  --pg data/rhodes-la-custom/samples.json data/rhodes-la-custom-rel/samples.json \
  --pg data/vintage-vibe-ep/samples.json \
  --pg --organ \
  --period-size 128 \
  --periods 3 \
  --status-socket /tmp/elepiano-status.sock \
  hw:0,0
Environment=ELEPIANO_IR_DIR=/home/hakaru/elepiano/data/ir
Environment=ELEPIANO_IR_WET=0.7
Restart=on-failure
RestartSec=3
LimitRTPRIO=80
LimitMEMLOCK=infinity

[Install]
WantedBy=multi-user.target
```

### ble-bridge.service の設定

`/etc/systemd/system/ble-bridge.service`（`ble/ble-bridge.service` を `/etc/systemd/system/` にコピー）:

```ini
[Unit]
Description=elepiano BLE Bridge
After=bluetooth.target elepiano.service
Wants=bluetooth.target

[Service]
Type=simple
Environment=PYTHONUNBUFFERED=1
ExecStart=/usr/bin/python3 /home/hakaru/elepiano/ble/ble_bridge.py
WorkingDirectory=/home/hakaru/elepiano/ble
Restart=on-failure
RestartSec=3
User=root

[Install]
WantedBy=multi-user.target
```

### サービスの有効化と起動

```bash
# サービスファイルのコピー（必要な場合）
sudo cp ~/elepiano/ble/ble-bridge.service /etc/systemd/system/

# デーモン再読み込み
sudo systemctl daemon-reload

# 自動起動を有効化
sudo systemctl enable elepiano.service
sudo systemctl enable ble-bridge.service

# 起動
sudo systemctl start elepiano.service
sudo systemctl start ble-bridge.service
```

### サービス管理コマンド

```bash
# ステータス確認
sudo systemctl status elepiano
sudo systemctl status ble-bridge

# ログ確認
journalctl -fu elepiano
journalctl -fu ble-bridge

# 再起動
sudo systemctl restart elepiano
sudo systemctl restart ble-bridge

# 停止
sudo systemctl stop elepiano
sudo systemctl stop ble-bridge
```

---

## 環境変数一覧

elepiano は起動時に以下の環境変数を参照する。

### IR Convolver

| 変数名 | デフォルト | 説明 |
|--------|------------|------|
| `ELEPIANO_IR_DIR` | 未設定 | IR WAV ファイルのディレクトリ（アルファベット順で読み込み）|
| `ELEPIANO_IR_FILE` | 未設定 | 単一 IR WAV ファイルパス（`ELEPIANO_IR_DIR` がない場合のフォールバック）|
| `ELEPIANO_IR_WET` | 0.7 | IR ウェット/ドライ比率（0.0〜1.0）|

### LV2 プラグイン（ELEPIANO_ENABLE_LV2=1 ビルド時のみ）

| 変数名 | デフォルト | 説明 |
|--------|------------|------|
| `ELEPIANO_LV2_CHAIN` | 未設定 | IR 前の LV2 チェーン（セミコロン区切り URI リスト）|
| `ELEPIANO_LV2_POST_CHAIN` | 未設定 | IR 後の LV2 チェーン（セミコロン区切り URI リスト）|
| `ELEPIANO_LV2_URI` | 未設定 | 後方互換: 単一 LV2 URI（pre chain に追加）|
| `ELEPIANO_LV2_N_CC_MAP` | 未設定 | N 番目プラグインの CC マップ（例: `70=2,71=3`）|
| `ELEPIANO_LV2_N_WET` | 1.0 | N 番目プラグインのウェットレベル（0.0〜1.0）|
| `ELEPIANO_LV2_N_DEFAULTS` | 未設定 | N 番目プラグインのポートデフォルト値（例: `port=value,...`）|
| `ELEPIANO_LV2_CC_MAP` | 未設定 | 後方互換: index 0 プラグインの CC マップ |
| `ELEPIANO_LV2_WET` | 未設定 | 後方互換: index 0 プラグインのウェット |

N は 0 から始まる通し番号（pre chain: 0, 1, ...; post chain: pre_count, pre_count+1, ...）。

### setBfree オルガン設定

| 変数名 | デフォルト | 説明 |
|--------|------------|------|
| `ELEPIANO_ORGAN_CFG` | `data/organ/vb3-style.cfg` | setBfree 設定ファイルパス |

### MIDI デバッグ

| 変数名 | デフォルト | 説明 |
|--------|------------|------|
| `ELEPIANO_MIDI_LOG` | 未設定 | `1` に設定すると全 MIDI イベントを stderr に出力 |

---

## コマンドラインオプション

```
elepiano [オプション] [音色定義...] [alsa_device]

オプション:
  --organ [device]         オルガンモードで起動（setBfree 物理モデル）
  --pg <attack.json> [release.json]
                           ピアノ音色を追加（最大 16 音色）
  --pg --organ             オルガンプログラムをピアノ音色リストに追加
  --period-size <N>        ALSA バッファサイズ（サンプル数、16〜8192、デフォルト 128）
  --periods <N>            ALSA バッファ数（2〜8、デフォルト 3）
  --status-socket <path>   ステータス UNIX socket のパス
  --wav-dump <path>        オーディオ出力を WAV ファイルにダンプ
  --midi-log               MIDI イベントをログ出力
```

### 起動例

```bash
# ピアノ単体（Rhodes のみ）
elepiano data/rhodes-classic/samples.json hw:0,0

# 複数音色（Rhodes + LA Custom + Vintage Vibe + Organ）
elepiano \
  --pg data/rhodes-classic/samples.json data/rhodes-classic-rel/samples.json \
  --pg data/rhodes-la-custom/samples.json data/rhodes-la-custom-rel/samples.json \
  --pg data/vintage-vibe-ep/samples.json \
  --pg --organ \
  --period-size 128 --periods 3 \
  --status-socket /tmp/elepiano-status.sock \
  hw:0,0

# IR + LV2 有効（ELEPIANO_ENABLE_LV2=1 ビルド時）
ELEPIANO_IR_DIR=/home/hakaru/elepiano/data/ir \
ELEPIANO_IR_WET=0.7 \
ELEPIANO_LV2_CHAIN="https://example.com/plugin/preamp" \
  elepiano data/rhodes-classic/samples.json hw:0,0
```

---

## 起動確認

### elepiano プロセスの確認

```bash
# 起動ログ確認
journalctl -fu elepiano | head -30

# 期待される出力:
# [SampleDB] Loaded N samples from data/...
# [AudioOutput] SCHED_FIFO priority=80
# [FxChain] IR convolver enabled (N IRs, active: ...)
# 起動完了。Ctrl+C で終了。
```

### BLE Bridge の確認

```bash
journalctl -fu ble-bridge | head -20

# 期待される出力:
# [GATT] application registered
# [GATT] advertisement registered
# [StatusMonitor] connected to /tmp/elepiano-status.sock
```

### iOS アプリからの接続確認

1. iOS アプリを起動
2. 「Scanning...」が表示される
3. 数秒後に自動接続されてメイン画面（LandscapeCCView）に遷移
4. 右上のステータスに音色名とボイス数が表示されれば正常

### ALSA デバイスの確認

```bash
# 利用可能なデバイス一覧
aplay -l

# MIDI デバイス確認
aplaymidi -l
```

### RT スケジューリングの確認

```bash
# オーディオスレッドの優先度確認（起動後に実行）
ps -eo pid,cls,rtprio,comm | grep elepiano
```

`FIFO 80` と表示されれば SCHED_FIFO が有効。表示されない場合は `LimitRTPRIO=80` が systemd に設定されているか確認する。
