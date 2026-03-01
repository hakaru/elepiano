# MIDI 2.0 Property Exchange (PE) 対応 — Raspberry Pi 5

> 調査日: 2026-03-02
> カーネル: rpi-6.6.y (Raspberry Pi OS Bookworm)

---

## 概要

Raspberry Pi 5 を **USB MIDI 2.0 クラスコンプライアントデバイス** として動作させ、
接続した Mac/PC の DAW に対して **Property Exchange (PE)** でパッチ情報を送信する構成。

```
MIDI 2.0 ホスト (Mac/PC DAW)
        ↕ USB-C (MIDI 2.0 class compliant)
Raspberry Pi 5
  └── elepiano (MIDI 2.0 UMP + PE 実装)
        ↕ ALSA UMP rawmidi
  Linux kernel (USB MIDI 2.0 gadget)
```

---

## カーネル設定調査結果

### ソース状況 (rpi-6.6.y)

| ファイル | 状態 |
|---------|------|
| `drivers/usb/gadget/function/f_midi2.c` | ✅ 存在 |
| `drivers/usb/gadget/function/u_midi2.h` | ✅ 存在 |
| `CONFIG_USB_CONFIGFS_F_MIDI2` (Kconfig) | ✅ 定義あり |

### デフォルト kernel (bcm2712_defconfig) での有効状態

| 設定 | 状態 | 備考 |
|------|------|------|
| `CONFIG_USB_CONFIGFS` | ✅ `=m` | 有効 |
| `CONFIG_USB_CONFIGFS_F_MIDI` | ✅ `=y` | MIDI 1.0 ガジェット (有効) |
| `CONFIG_USB_CONFIGFS_F_MIDI2` | ❌ 未記載 | **要追加** |
| `CONFIG_SND_UMP` | ❌ 未記載 | **要追加** |
| `CONFIG_SND_USB_AUDIO_MIDI_V2` | ❌ 未記載 | **要追加** |

**→ デフォルト Raspberry Pi OS では MIDI 2.0 ガジェットは無効。カーネル再ビルドが必要。**

---

## カーネル再ビルド手順

### 1. ビルド環境構築 (Pi 5 上)

```bash
sudo apt install -y git bc bison flex libssl-dev make gcc
sudo apt install -y raspberrypi-kernel-headers

git clone --depth=1 -b rpi-6.6.y https://github.com/raspberrypi/linux
cd linux
```

### 2. ベース設定 + MIDI 2.0 オプション追加

```bash
make bcm2712_defconfig

# MIDI 2.0 ガジェットを有効化
scripts/config --enable CONFIG_USB_CONFIGFS_F_MIDI2
scripts/config --enable CONFIG_SND_UMP
scripts/config --enable CONFIG_SND_USB_AUDIO_MIDI_V2
# UMP レガシー rawmidi (MIDI 1.0 互換レイヤー)
scripts/config --enable CONFIG_SND_UMP_LEGACY_RAWMIDI

make olddefconfig
```

### 3. ビルド & インストール

```bash
# Pi 5 (4コア) でビルド — 約1〜2時間
make -j4 Image modules dtbs

sudo make modules_install
sudo cp arch/arm64/boot/Image /boot/firmware/kernel_2712.img
sudo cp arch/arm64/boot/dts/broadcom/bcm2712*.dtb /boot/firmware/
sudo reboot
```

### 4. 再起動後の確認

```bash
# F_MIDI2 モジュールが存在するか確認
modinfo usb_f_midi2

# UMP サポート確認
cat /proc/asound/version
```

---

## USB ガジェットモードのセットアップ

### Pi 5 の制約

- **USB-C ポートのみ**ガジェットモード対応（USB-A 4本はホスト専用）
- USB 2.0 のみ（USB 3.0 不可）
- USB-C をデータ用に使うと電源供給が制限される

### 電源対策

```
[電源アダプター] → [USB 電源スプリッターケーブル] → [Pi 5 USB-C 電源ピン]
                                                  → [Mac/PC USB-C]  ← データのみ
```

### /boot/firmware/config.txt 設定

```ini
# USB ガジェットモード有効化
dtoverlay=dwc2,dr_mode=peripheral
```

### configfs でガジェット定義

```bash
#!/bin/bash
# /usr/local/bin/setup-midi2-gadget.sh

GADGET_DIR=/sys/kernel/config/usb_gadget/elepiano
modprobe libcomposite

mkdir -p $GADGET_DIR
cd $GADGET_DIR

# USB デバイス情報
echo 0x1d6b > idVendor   # Linux Foundation
echo 0x0104 > idProduct  # Multifunction Composite Gadget
echo 0x0200 > bcdUSB     # USB 2.0
echo 0xEF   > bDeviceClass
echo 0x02   > bDeviceSubClass
echo 0x01   > bDeviceProtocol

# 文字列
mkdir -p strings/0x409
echo "hakaru"            > strings/0x409/manufacturer
echo "elepiano"          > strings/0x409/product
echo "elepiano-001"      > strings/0x409/serialnumber

# MIDI 2.0 ファンクション
mkdir -p functions/midi2.usb0
echo "elepiano"          > functions/midi2.usb0/ep_name
echo "elepiano Synth"    > functions/midi2.usb0/ep_product_id

# UMP Function Block (1ブロック = 1 MIDI ポート)
mkdir -p functions/midi2.usb0/ep.0/fb.0
echo "Rhodes"            > functions/midi2.usb0/ep.0/fb.0/name
echo 0                   > functions/midi2.usb0/ep.0/fb.0/direction  # bidirectional
echo 1                   > functions/midi2.usb0/ep.0/fb.0/first_group
echo 1                   > functions/midi2.usb0/ep.0/fb.0/num_groups

# コンフィグ
mkdir -p configs/c.1/strings/0x409
echo "MIDI 2.0"          > configs/c.1/strings/0x409/configuration
echo 250                 > configs/c.1/MaxPower
ln -s functions/midi2.usb0 configs/c.1/

# UDC (USB Device Controller) に接続
ls /sys/class/udc > /tmp/udc_list
echo $(cat /tmp/udc_list) > UDC

echo "MIDI 2.0 gadget 起動完了"
```

```bash
sudo chmod +x /usr/local/bin/setup-midi2-gadget.sh
# 起動時自動実行: /etc/rc.local に追記
```

---

## Property Exchange (PE) 実装方針

### カーネルの責務とユーザースペースの責務

```
カーネル (USB MIDI 2.0 ガジェット)
  - UMP パケット送受信
  - UMP Stream メッセージ応答 (Endpoint/Function Block 情報)

ユーザースペース (elepiano)
  - MIDI-CI ネゴシエーション      ← 実装が必要
  - Property Exchange (GET/SET)   ← 実装が必要
    - パッチリストの公開
    - パラメーターの公開/操作
```

### 推奨ライブラリ: AM_MIDI2.0Lib

```
https://github.com/midi2-dev/AM_MIDI2.0Lib
```

- C++ / ヘッダーオンリー対応
- MIDI-CI、UMP、PE を実装
- Linux 対応

### UMP rawmidi 経由の接続

```
elepiano (C++)
  └── /dev/umidi0D0  (UMP rawmidi デバイス)
        ↕ read/write
  カーネル USB MIDI 2.0 ガジェット
        ↕ USB
  Mac/PC DAW
```

---

## 接続先の対応要件

| ホスト | MIDI 2.0 対応 |
|--------|--------------|
| macOS 14+ (Sonoma) | ✅ CoreMIDI 2.0 対応 |
| Windows 11 22H2+ | ✅ Windows MIDI Services 対応 |
| macOS 13 以前 | ❌ MIDI 1.0 フォールバックのみ |
| Linux (kernel 6.5+) | ✅ UMP 対応 |

---

## 実装ロードマップ

- [ ] **Step 1**: カーネル再ビルド (`F_MIDI2`, `SND_UMP` 有効化)
- [ ] **Step 2**: `setup-midi2-gadget.sh` でガジェット起動確認
- [ ] **Step 3**: Mac から MIDI 2.0 デバイスとして認識されるか確認
- [ ] **Step 4**: AM_MIDI2.0Lib を elepiano に組み込み
- [ ] **Step 5**: PE でパッチリスト (samples.json) を公開
- [ ] **Step 6**: PE SET でパラメーター制御（ゲイン、エフェクトなど）

---

## 参考リンク

- [MIDI 2.0 on Linux — Kernel docs](https://docs.kernel.org/sound/designs/midi-2.0.html)
- [RPi USB Gadget MIDI forum](https://forums.raspberrypi.com/viewtopic.php?t=374091)
- [AM_MIDI2.0Lib](https://github.com/midi2-dev/AM_MIDI2.0Lib)
- [MIDI 2.0 Workbench (テストツール)](https://github.com/midi2-dev/MIDI2.0Workbench)
- [midi2kit.dev](https://midi2kit.dev/) ※ Swift / macOS のみ、Pi 不可
