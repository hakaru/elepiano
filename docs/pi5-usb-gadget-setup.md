# Pi 5 セットアップ手順

> 作成日: 2026-03-03
> 更新日: 2026-03-04
> 対象: Raspberry Pi 5 + Pi OS Lite 64-bit (Bookworm) + Mac ホスト

---

## 概要

Pi 5 を Wi-Fi 経由で Mac に接続し、SSH でログインする手順。
elepiano エンジンのビルド・実行環境として使用する。

> **注意:** Pi 5 の USB-C ポートは電源専用。Pi Zero/4 で使える dwc2 USB gadget モードは
> Pi 5 では利用不可（RP1 チップ経由 xHCI = ホストモード専用）。

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
wifis:
  wlan0:
    dhcp4: true
    access-points:
      "YOUR_SSID":
        password: "YOUR_PASSWORD"
```

---

## 3. Mac 側の設定

### /etc/hosts (オプション)

```
192.168.3.193   hakarupiano
```

Pi は Wi-Fi (`wlan0`) で DHCP から IP を取得する。
ルーター側で IP 固定するか、mDNS (`hakarupiano.local`) で接続する。

---

## 4. 接続確認手順

```bash
# ping
ping hakarupiano.local

# SSH
ssh hakaru@192.168.3.193
```

---

## トラブルシューティング

### SSH 接続できない (Tailscale 環境)

**症状:**
- `tcpdump` で SYN パケットが出ていない
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

- [x] SSH 接続確認 (`ssh hakaru@192.168.3.193`)
- [x] 依存ライブラリインストール確認 (`libasound2-dev`, `libflac-dev`, `nlohmann-json3-dev`, `cmake`)
- [x] elepiano ソース転送 & ビルド (GCC 14 対応: コンストラクタオーバーロード化)
- [x] KORG Keystage 接続 (USB MIDI + USB Audio: `hw:Keystage`)
- [x] MIDI 接続テスト — オルガンモードで音出し確認
