# Tailscale macOS ローカルネットワーク干渉 調査レポート

作成日: 2026-03-03
調査背景: elepiano プロジェクトで Raspberry Pi5 を USB Gadget モード（USB-C 経由の Ethernet）で Mac に接続し SSH 接続しようとした際、Tailscale の Network Extension がパケットをインターセプトして ARP が incomplete のまま接続不能になった問題

---

## 1. Tailscale Network Extension の仕組み

### macOS での Tailscale の実行方式（3種類）

| 方式 | 技術 | 用途 |
|------|------|------|
| Standalone (推奨) | System Extension (macOS 10.15+) | 一般的な PKG インストール |
| Mac App Store 版 | Network Extension (NEPacketTunnelProvider) | サンドボックス制限あり |
| OSS tailscaled | カーネル utun インターフェース | 上級者向け、CLI のみ |

参照: [Three ways to run Tailscale on macOS](https://tailscale.com/kb/1065/macos-variants)

### Network Extension フレームワークの役割

- Apple の **NEPacketTunnelProvider** API を使い、macOS カーネルの utun 仮想インターフェース経由でパケットを仲介する
- System Extension（Standalone 版）はカーネル拡張の後継で、root 権限を持つがサンドボックス内で動作
- **ルーティングテーブルにエントリを注入**してトラフィックを utun デバイス経由にリダイレクトする

参照: [TN3120: Expected use cases for Network Extension packet tunnel providers](https://developer.apple.com/documentation/technotes/tn3120-expected-use-cases-for-network-extension-packet-tunnel-providers)

### パケット処理パイプライン

```
アプリ送信パケット
  ↓
macOS カーネル ネットワークスタック
  ↓
Tailscale Network Extension (utun インターフェース)
  ↓ プリフィルタ → ACL フィルタ → ポストフィルタ
WireGuard 暗号化エンジン
  ↓
物理 NIC（Wi-Fi / Ethernet）
```

- **macOS 固有の問題**: NE は自ノード宛パケットをトンネル内にリダイレクトするため、ループバックリフレクション処理が必要
- ローカルネットワーク宛トラフィックも utun を経由する可能性がある

参照: [TUN Device and Packet Filtering | DeepWiki](https://deepwiki.com/tailscale/tailscale/4.5-tun-device-and-packet-filtering)

### `tailscale down` / `tailscale stop` しても NE が残る理由

これが最重要ポイント。

- `tailscale down` は **Tailscale の論理接続を切断するだけ**であり、Network Extension プロセス自体は継続動作する
- GUI アプリを「終了」しても "Leave VPN Active" オプション時は NE が残る
- NE はカーネルに注入したルーティングエントリを**保持したまま**になる
- これにより USB Gadget（en5 等）のローカル通信が引き続き utun 経由にルーティングされ、ARP が通らない

`tailscale down` で切れるもの:
- Tailscale ノードの接続状態（Tailnet への参加）
- WireGuard トンネル

`tailscale down` で切れないもの:
- System Extension プロセス（io.tailscale.ipn.macsys.network-extension）
- カーネルへの utun インターフェース登録
- ルーティングテーブルへの注入エントリ（残留する可能性）

---

## 2. Tailscale が問題になる既知のケース

### USB Gadget / USB Ethernet アダプター（今回の問題）

- Raspberry Pi を CDC-ECM / RNDIS USB Gadget として Mac に接続すると、新しい仮想 Ethernet インターフェース（例: en5）が作成される
- Tailscale の NE がルーティングテーブルを操作することで、この `en5` 宛のパケットが utun 経由に誤ルートされる
- ARP リクエストが Pi に届かない → ARP テーブルが incomplete のまま → TCP セッション確立不可
- **`tailscale down` では NE が残留するため解決しない**

参照:
- [Raspberry Pi USB Gadget GitHub](https://github.com/raspberrypi/rpi-usb-gadget)
- [Having issues with MacOS Catalina and gadget mode](https://forums.raspberrypi.com/viewtopic.php?t=255000)

### ローカル LAN 通信（同一サブネット）

- Tailscale がサブネットルーターとして機能するノードを tailnet 上に持つ場合、同一 LAN のアドレス宛トラフィックが Tailscale 経由にルートされることがある
- macOS では **最長一致ルート優先**のはずだが、NE が注入したルートが優先される場合がある

```
Issue #846: macOS: exactly matching subnet routes don't override physical interface routes
```

参照: [Mac userspace subnet router binding to wrong interface #5719](https://github.com/tailscale/tailscale/issues/5719)

### Exit Node 使用時のローカルアクセス障害

- Exit Node を使用すると **デフォルトでローカル LAN アクセスが遮断**される
- `172.16/12` 等のプライベートアドレス宛ルートが誤った NIC（en0 等）に向く症例が報告されている
- `--exit-node-allow-lan-access=true` で緩和できるが完全ではないケースがある

```
正常時: 172.16/12 link#16 UCS bridge0
問題時: 172.16/12 192.168.0.1 UGSc en0  ← Tailscale が誤挿入
```

参照: [macOS exit node can't access local networks #15366](https://github.com/tailscale/tailscale/issues/15366)

### Docker / VM ブリッジネットワーク

- Docker 28 で `/etc/resolv.conf` の扱いが変わり、Tailscale が書き換えた DNS 設定（100.100.100.100）がコンテナに伝播しない問題が発生
- ブリッジネットワーク経由のコンテナ間通信が Tailscale の DNS 書き換えにより障害を受けることがある

参照: [Docker 28 stops containers communicating with tailscale network #49498](https://github.com/moby/moby/issues/49498)

### AirDrop / Bonjour / mDNS

- Tailscale はマルチキャスト（mDNS）をトンネル越しに転送しない
- ただし **ローカル mDNS 自体への干渉**は限定的（tailnet 越しに転送されないだけ）
- Bonjour を tailnet 越しに使いたい場合は別途設定が必要（未実装の Feature Request 状態）

参照: [Support mDNS for name and service resolution #1013](https://github.com/tailscale/tailscale/issues/1013)

### VPN 併用時

- Tailscale と他 VPN の同時使用は基本的に非推奨
- IP アドレス競合: 他 VPN が `100.64.0.0/10` を使うと Tailscale の CGNAT アドレス範囲と衝突
- ルーティング競合: どちらの VPN がデフォルトルートを持つかで動作が変わる

参照: [Can I use Tailscale alongside other VPNs?](https://tailscale.com/kb/1105/other-vpns)

### 開発サーバー（localhost 以外のバインド）

- Tailscale の MagicDNS が `/etc/resolv.conf` を書き換えることで、ローカルの DNS 解決に影響する場合がある
- Split DNS 設定時に Split DNS がローカルルーティングテーブルを無視する問題が報告されている

参照: [Split DNS does not respect local routing table on macOS #12940](https://github.com/tailscale/tailscale/issues/12940)

---

## 3. 回避策・共存方法

### 方法 1: System Extension を一時的に無効化（推奨・最も確実）

Standalone 版（PKG インストール）の場合:

```bash
# 無効化
tailscale configure sysext deactivate

# 状態確認
tailscale configure sysext status

# 再有効化
tailscale configure sysext activate
```

GUI からの操作:
System Settings > General > Login Items & Extensions > Network Extensions
→ "Tailscale Network Extension" をオフ

参照: [Authorizing the Tailscale system extension on macOS](https://tailscale.com/kb/1340/macos-sysext)

**注意**: この方法は Standalone 版専用。App Store 版では CLI コマンドが異なる場合がある。

### 方法 2: ルーティングテーブルを手動修正（一時的な回避）

NE が誤ったルートを注入した場合の応急処置:

```bash
# 現在のルーティングテーブル確認
netstat -rn

# 誤ったルートを削除して正しい経路を追加（例: USB Gadget が en5 の場合）
sudo route delete -net 169.254.0.0/16 -interface en0
sudo route add -net 169.254.0.0/16 -interface en5

# ARP テーブル確認
arp -a
```

### 方法 3: `--accept-routes=false` で subnet routes を無効化

他の tailnet ノードが広告しているサブネットルートを受け入れないようにする:

```bash
tailscale up --accept-routes=false
```

これで tailnet 上の subnet router 経由のルート注入を防げるが、NE 自体の影響は残る。

### 方法 4: Exit Node を使わない + LAN アクセスを明示的に許可

Exit Node を使用している場合:

```bash
tailscale up --exit-node=<node> --exit-node-allow-lan-access=true
```

または CLI で:

```bash
tailscale set --exit-node-allow-lan-access=true
```

### 方法 5: OSS tailscaled 版に切り替える（上級者向け）

System Extension / Network Extension を使わず、カーネル utun を直接使う OSS 版は NE のサンドボックス制約がなく、ルーティング挙動が異なる:

```bash
# Homebrew でインストール
brew install tailscale

# tailscaled デーモンを起動
sudo tailscaled --tun=utun
tailscale up
```

参照: [Tailscaled on macOS](https://github.com/tailscale/tailscale/wiki/Tailscaled-on-macOS)

**macOS での Userspace Networking モードについての注意**:
`--tun=userspace-networking` は GUI 版では未対応（2025年5月時点でも macOS GUI 版は未サポート）。OSS tailscaled であれば理論上使用可能だが、macOS 固有の制限がある。

### 方法 6: USB Gadget 接続専用のルーティング設定（運用回避策）

Tailscale が動いたまま USB Gadget を使う場合、接続後に強制的にルートを修正するスクリプト:

```bash
#!/bin/bash
# USB Gadget 接続後に実行
IFACE="en5"  # USB Gadget のインターフェース名を確認して変更
GW="192.168.2.1"  # USB Gadget のゲートウェイ IP

# Tailscale が注入した可能性のあるルートを削除して再設定
sudo route delete -net 192.168.2.0/24
sudo route add -net 192.168.2.0/24 -interface ${IFACE}
```

---

## 4. 最新情報（2025-2026年）

### 現在も未解決の問題

- **macOS exit node でのローカル LAN アクセス障害** (Issue #15366, 2025年3月報告)
  → 最新版でも手動でのルーティングテーブル修正が必要

- **macOS 15 での kernel panic** (Issue #18325)
  → io.tailscale.ipn.macsys.network が原因の断続的な kernel panic

- **macOS Tahoe (26.x) での System Extension 孤立問題** (Issue #17891)
  → App Store 版と Standalone 版の混在状態で NE が完全に壊れる

- **macOS での Userspace Networking モード未対応** (Issue #16004, 2025年5月)
  → GUI 版ではサポートなし。OSS tailscaled でも macOS では制限あり

### 部分的に改善された点（2025年）

- macOS Share option 経由の Taildrop が正常動作するように修正
- System Extension のアップグレード時インストール失敗の修正
- カスタムコントロールサーバー（Headscale）との接続問題の修正

参照: [Tailscale Changelog](https://tailscale.com/changelog)

### Apple の Network Extension API の制限（根本的な原因）

Apple の NEPacketTunnelProvider には根本的な制限がある:

> "There are no ways in NEPacketTunnelProvider / NEPacketTunnelNetworkSettings to reroute traffic from the virtual utun interface to the physical interface based on packet inspection."

つまり、**パケット検査に基づいて物理インターフェースに選択的にリダイレクトする機能がそもそも存在しない**。これが USB Gadget 問題の根本原因であり、Tailscale 側だけでは完全に解決できない Apple の API 制約になっている。

参照: [Apple Developer Forums - NEPacketTunnelProvider route traffic](https://developer.apple.com/forums/thread/725048)

---

## 5. 推奨ワークフロー（elepiano プロジェクト向け）

### Pi5 USB Gadget 接続時の手順

```bash
# 1. Tailscale の System Extension を一時停止
tailscale configure sysext deactivate

# 2. USB-C ケーブルを接続
# 3. en5 (または該当インターフェース) が現れることを確認
ifconfig | grep en

# 4. SSH 接続
ssh hakaru@raspberrypi.local  # または直接 IP で接続

# 5. 作業完了後、Tailscale を再有効化
tailscale configure sysext activate
```

### インターフェース名の確認方法

```bash
# USB Gadget 接続前後でインターフェース一覧を比較
networksetup -listallhardwareports

# または
ifconfig -a | grep -E "^en"
```

---

## まとめ

| 項目 | 内容 |
|------|------|
| 根本原因 | NE が utun 経由でルーティングテーブルを操作し、USB Gadget の L3 通信を阻害 |
| `tailscale down` が効かない理由 | NE プロセス・ルート注入は継続するため |
| 最も確実な回避策 | `tailscale configure sysext deactivate` で NE を一時停止 |
| Apple API の制限 | NEPacketTunnelProvider はパケット検査ベースの選択的ルーティングが不可能 |
| 2026年現在の状況 | USB Gadget への根本対応は未実装。手動での NE 停止が必要 |

---

## 参考リンク

- [Three ways to run Tailscale on macOS](https://tailscale.com/kb/1065/macos-variants)
- [Authorizing the Tailscale system extension on macOS](https://tailscale.com/kb/1340/macos-sysext)
- [Can't connect to local area network](https://tailscale.com/kb/1636/connect-lan-failure)
- [Can I use Tailscale alongside other VPNs?](https://tailscale.com/kb/1105/other-vpns)
- [Tailscaled on macOS Wiki](https://github.com/tailscale/tailscale/wiki/Tailscaled-on-macOS)
- [macOS: routing via exit-node or subnet-router bypasses routing table #7625](https://github.com/tailscale/tailscale/issues/7625)
- [macOS exit node can't access local networks #15366](https://github.com/tailscale/tailscale/issues/15366)
- [Downstream device can't access network when upstream Ethernet Gadget using Exit Node #13522](https://github.com/tailscale/tailscale/issues/13522)
- [TUN Device and Packet Filtering | DeepWiki](https://deepwiki.com/tailscale/tailscale/4.5-tun-device-and-packet-filtering)
- [TN3120: Expected use cases for Network Extension packet tunnel providers](https://developer.apple.com/documentation/technotes/tn3120-expected-use-cases-for-network-extension-packet-tunnel-providers)
