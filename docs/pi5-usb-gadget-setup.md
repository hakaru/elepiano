# Pi 5 USB Gadget セットアップ手順

> 作成日: 2026-03-03
> 対象: Raspberry Pi 5 + Pi OS Lite 64-bit (Bookworm) + Mac ホスト

---

## 概要

Pi 5 を USB-C 経由で Mac に接続し、`dwc2` USB gadget として動作させる手順。
SSH over USB (`usb0` インターフェース) を最初のゴールとする。

---

## 1. Pi OS インストール

### 使用イメージ

```
Raspberry Pi OS Lite (64-bit) — Bookworm
```

### SD カードへの書き込み

```bash
# デバイス確認
diskutil list

# アンマウント (例: /dev/disk4)
diskutil unmountDisk /dev/disk4

# 書き込み
sudo dd if=raspios-bookworm-arm64-lite.img of=/dev/rdisk4 bs=4m status=progress
```

---

## 2. cloud-init による初期設定

Pi OS Bookworm は cloud-init を使った初期設定に対応している。
SD カードの `bootfs` パーティション (`/Volumes/bootfs`) に以下のファイルを配置する。

### `/Volumes/bootfs/user-data`

```yaml
#cloud-config

hostname: hakarupiano

ssh_pwauth: false

users:
  - name: hakaru
    groups: sudo
    shell: /bin/bash
    sudo: ALL=(ALL) NOPASSWD:ALL
    ssh_authorized_keys:
      - ssh-ed25519 AAAA... (your public key)

packages:
  - libasound2-dev
  - libflac-dev
  - nlohmann-json3-dev
  - cmake
  - git

enable_ssh: true
```

> **注意:** `bootcmd` で `systemctl start ssh` を実行してはいけない。
> `network.target` 待ちでハングし再起動ループを引き起こす。
> `enable_ssh: true` だけで十分。

### `/Volumes/bootfs/network-config`

```yaml
version: 2
ethernets:
  usb0:
    dhcp4: true
    optional: true
```

---

## 3. USB Gadget (dwc2) 設定

SD カードの `bootfs` パーティションを Mac でマウントして編集する。

### `/Volumes/bootfs/config.txt` に追記

```ini
# USB gadget モード (Pi 5 USB-C ポート)
dtoverlay=dwc2,dr_mode=peripheral
```

### `/Volumes/bootfs/cmdline.txt` に `modules-load=dwc2` を追記

```
# 既存の行の末尾（改行なし）に追加
... rootwait modules-load=dwc2
```

> **確認ポイント:** `cmdline.txt` は1行のみ。改行を入れると起動しない。

---

## 4. Mac 側の設定

### /etc/hosts (オプション)

```
192.168.x.x   hakarupiano.local hakarupiano
```

USB 経由の場合、Pi は `usb0` に DHCP で IP を取得する。
`hakarupiano.local` は mDNS で解決される。

---

## 5. 接続確認手順

### Pi 起動後の確認

```bash
# Mac から Pi が USB デバイスとして見えているか
system_profiler SPUSBDataType | grep -A5 "Raspberry"

# ARP 確認
arp -a | grep en5   # en5 = USB gadget ネットワークインターフェース

# ping
ping hakarupiano.local

# SSH
ssh hakaru@hakarupiano.local
```

---

## トラブルシューティング

### Pi が USB デバイスとして Mac に認識されない

**症状:**
- `system_profiler SPUSBDataType` に Raspberry Pi のエントリなし
- `arp -a` で ARP incomplete

**原因と対処:**

| 原因 | 対処 |
|------|------|
| `config.txt` に `dtoverlay=dwc2` がない | 上記 §3 を適用 |
| `cmdline.txt` に `modules-load=dwc2` がない | 上記 §3 を適用 |
| USB-C ケーブルがデータ転送非対応 | データ対応ケーブルに交換 |

### SSH 接続できない (Tailscale 環境)

**症状:**
- `tcpdump` で SYN パケットが `en5` に出ていない
- `arp -a` で ARP incomplete のまま

**原因:**
Tailscale の Network Extension (`io.tailscale.ipn.macsys.network-extension`) が
カーネルレベルで全 TCP パケットをインターセプトしている。
`tailscale stop` では除去できない。

**対処:**
1. Tailscale をアンインストール（System Extension の除去が必要）
2. Mac を再起動

```bash
# アンインストール後の確認
systemextensionsctl list | grep tailscale
# → 何も出なければ OK
```

### cloud-init bootcmd で SSH が起動しない

**症状:**
- Pi 起動後10分以上 SSH に繋がらない
- `cloud-init-output.log` に `systemctl start ssh` がタイムアウト

**原因:**
`bootcmd` は `cloud-init-local` ステージで実行されるため、
`network.target` が未完了の状態で `systemctl start ssh` を呼ぶとハングする。

**対処:**
`user-data` から `bootcmd` を削除し、`enable_ssh: true` のみ残す。

---

## 次のステップ

- [ ] SSH 接続確認 (`ssh hakaru@hakarupiano.local`)
- [ ] 依存ライブラリインストール確認 (`libasound2-dev`, `libflac-dev` など)
- [ ] elepiano ソース転送 & ビルド
- [ ] MIDI 接続テスト (PiSound または USB MIDI)
- [ ] MIDI 2.0 gadget 対応 → [`midi2-pe-setup.md`](./midi2-pe-setup.md) 参照
