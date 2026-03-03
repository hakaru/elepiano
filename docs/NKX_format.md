# NKX / NKC フォーマット解析ノート

**対象:** NI Kontakt ライブラリ (Scarbee Vintage Keys_Mark I.nkx を実ファイルで解析)
**解析日:** 2026-03-02
**結論:** 音声データは AES-128-CBC 暗号化。鍵なしでの抽出は不可能。

---

## 1. ファイル構成

Kontakt ライブラリは通常以下のセットで配布される。

| 拡張子 | 役割 | 暗号化 |
|--------|------|--------|
| `.nkx` | 音声サンプルコンテナ (大容量) | 音声データは AES-128-CBC |
| `.nkc` | メタデータキャッシュ (小容量) | 非暗号化 (構造化テキスト) |
| `.nkr` | リソースコンテナ (画像/スクリプト/IR) | AES暗号化 (~8.0 bits/byte) |
| `.nki` | 楽器定義 (Zone/Modulation マップ) | AES暗号化 (~7.98 bits/byte) |

### 実測サイズ (Scarbee Vintage Keys Mark I)

```
Scarbee Vintage Keys_Mark I.nkx   1,345 MB   (音声コンテナ)
Scarbee Vintage Keys_Mark I.nkc     117 KB   (メタデータ)
```

---

## 2. NKX ファイル構造

```
NKX File
├── Section A: Primary TOC         (offset 0 〜 95,690 bytes)
│   ├── Global Header  (44 bytes)   magic + library name "Mark I"
│   ├── TOC Header     (22 bytes)   magic + エントリ数
│   └── TOC Records    × 1692      (filename + data_offset)
│
├── Section B: Gap / Secondary     (offset 95,690 〜 181,094 bytes)
│   └── (未解析セクション / 別メタデータ)
│
└── Section C: Per-file Blocks     (offset 181,094 〜 EOF)
    ├── Block[0]: 31-byte header + v2 bytes audio
    ├── Block[1]: 31-byte header + v2 bytes audio
    └── ...
```

---

## 3. Global Header (offset 0, 44 bytes)

```
Offset  Size  Value                 説明
0       4     54 ac 70 5e           NKX magic
4       2     10 01 (= 0x0110)      バージョン?
6       2     a6 01 (= 0x01a6)      ライブラリID / エントリ数?
8       6     (flags)
14      2     01 00                 ?
16      6     (zeros)
22      2     16 00 (= 22)          次ヘッダーのサイズ?
24      4     2c 00 00 00 (= 44)    次セクションのオフセット
28      2     01 00
30      N     "Mark I" (UTF-16LE)   ライブラリ名
```

---

## 4. TOC Record フォーマット

TOC エントリは offset 66 から始まり、以下の可変長レコードが 1692 件並ぶ。

```c
struct TocRecord {
    uint16_t rec_size;        // レコード全体のサイズ (8〜512)
    uint32_t data_offset;     // ※ファイル内オフセットではない (後述)
    uint16_t unknown;         // 常に 0x0002
    wchar_t  name[];          // UTF-16LE ファイル名 (\x00\x00 終端)
};
```

**注意:** `data_offset` は実際のファイル内バイトオフセットではない。
Mark I の最大値は `0x4ffe9e74` (~1.34 GB) でファイルサイズとほぼ等しいが、
一部のエントリは `0x5fc...` (ファイルサイズ 1.31 GB を超える値) になり矛盾する。
用途は未解明（メモリマップアドレスやストリーミング用の仮想オフセットと推測）。

---

## 5. NKC ファイル構造 (メタデータキャッシュ)

NKC は NKX に対応するメタデータを保持する。**非暗号化 (entropy 3.75 bits/byte)**。

### NKC ヘッダー (offset 0〜0x53)

```
Offset  Size  Value              説明
0       4     7a 10 e1 3f        NKC magic (NKXとは異なる)
4       2     10 01              バージョン
34      4     9c 06 00 00        エントリ数 (= 1692 = 0x69c)
48      4     ca 75 01 00        NKX TOC 終端オフセット (= 95,690)
```

### NKC エントリ (offset 0x54〜, 1691件)

```c
struct NkcEntry {
    uint8_t  marker[4];      // 02 00 8b 77 (固定マーカー)
    uint8_t  unknown[8];     // 00 * 8
    uint32_t name_len;       // ファイル名の文字数
    wchar_t  name[];         // UTF-16LE ファイル名
    uint32_t v1;             // NKX 内の per-file block 開始オフセット ★
    uint32_t v2;             // 音声データサイズ (bytes) ★
};
```

**v1 と v2 の関係:**

```
v1[i+1] = v1[i] + 31 + v2[i]
```

すなわち各 per-file block は:
- 31 バイトのブロックヘッダー
- v2 バイトの音声データ

が連続して配置されている。

---

## 6. Per-file Block ヘッダー (31 bytes)

```
Offset  Size  Value                       説明
0       4     0a f8 cc 16                 セクション magic
4       2     10 01                       バージョン (= 0x0110)
6       2     a6 01                       ライブラリID (= 0x01a6)
8       11    00 00 00 01 00 00 03 00 ...  フラグ類 (全ファイル共通)
19      4     (uint32 LE)                 音声データサイズ = NKC v2 と一致
23      8     00 * 8                      パディング
```

ヘッダーの検証 (Scarbee Mark I):

| ファイル名 | v2 (NKC) | ヘッダー[19:23] |
|-----------|---------|----------------|
| RSP73 NOISE.ncw | 43,396 | `84 a9 00 00` (= 43396) |
| RSP73 rel h1 A#1 -0.ncw | 41,432 | `d8 a1 00 00` (= 41432) |
| RSP73 rel h1 A#2 -0.ncw | 39,580 | `9c 9a 00 00` (= 39580) |

---

## 7. 音声データの暗号化

**結論: AES-128-CBC 暗号化**

### 証拠 1: 共通 IV

全 1692 ファイルの音声データ先頭 16 バイトが完全一致:

```
fb ed 4f 72 e1 c3 7f 02 fb 2c f8 bb af d1 62 c5
```

これは AES-CBC の初期化ベクタ (IV) が全ファイルで共通であることを示す。

### 証拠 2: エントロピー

```
ファイル                     entropy
RSP73 NOISE.ncw          8.00 bits/byte
RSP73 sus h1 C4 -0.ncw   8.00 bits/byte  ← ピアノ持続音
```

- NCW 圧縮のみなら持続音は 6〜7 bits/byte になるはず
- NOISE も持続音も同じ 8.00 → AES 暗号化の均一性

### 証拠 3: マジックバイト不在

`+NCW` (0x2B 0x4E 0x43 0x57) の出現: **0件**
(ファイル全体 1.31 GB を全文検索)

### 暗号化パラメータ (推定)

| 項目 | 値 |
|------|----|
| アルゴリズム | AES-128-CBC |
| IV | `fb ed 4f 72 e1 c3 7f 02 fb 2c f8 bb af d1 62 c5` |
| 鍵 | NI ライセンスシステムから動的に取得 (マシン固有 or NI アカウント紐付け) |
| 鍵の保存場所 | `.nki` 内 or Kontakt エンジン内 (いずれも非公開) |

---

## 8. エントロピー測定の詳細

NOISE.ncw (v2 = 43,396 bytes) のバイト範囲別エントロピー:

```
[  0:  16]  3.88 bits/byte   ← IV (全ファイル共通のため内部多様性が低い)
[ 16:  32]  4.00 bits/byte   ← 暗号化第1ブロックの先頭
[ 32:  64]  4.88 bits/byte
[ 64: 128]  5.81 bits/byte
[128: 512]  7.49 bits/byte
[512:4096]  7.95 bits/byte
[  0: all]  8.00 bits/byte   ← 全体では AES 特有の均一高エントロピー
```

窓が小さい範囲で entropy が低く見えるのは、
16 バイトの IV 部分が統計を引き下げているためで、
暗号化データ自体は 8.0 bits/byte に収束している。

---

## 9. 他ファイルとの比較

| ファイル | エントロピー | 備考 |
|---------|------------|------|
| Keyscape .db (SpCA) 音声部 | ~7.5 bits/byte | XOR なし、FLAC 直格納 |
| NKX 音声データ | 8.00 bits/byte | AES-128-CBC |
| NKR (リソース) | 8.00 bits/byte | AES 暗号化 |
| NKI (楽器定義) | 7.98 bits/byte | AES 暗号化 |
| NKC (メタデータ) | 3.75 bits/byte | 非暗号化 ✅ |

---

## 10. 抽出可能性の評価

| 方法 | 実現可能性 |
|------|-----------|
| AES 鍵の総当たり | 不可 (AES-128 は事実上解読不能) |
| NKI から鍵を抽出 | NKI 自体が暗号化済み |
| Kontakt プロセスのメモリダンプ | 技術的には可能だが利用規約違反 |
| NI 公式 API / SDK | 存在しない |
| SpCA (Keyscape) 経由で同等素材 | **実用的。Rhodes / Wurlitzer 等をカバー** |

**結論: NKX 音声データの抽出は現実的に不可能。**
Keyscape STEAM (.db / SpCA) を引き続き使用する。

---

## 11. NKX TOC 内の data_offset フィールドについて

Primary TOC の `data_offset` フィールドはファイル内の実オフセットではない。
NKC v1 (= 実際の per-file block オフセット) との対応を調べたが規則性は見出せなかった。

考えられる解釈:
1. Kontakt がメモリマップ時に使用する仮想アドレス
2. NKX フォーマットの旧バージョン(インデックス形式)との互換フィールド
3. ストリーミング用の別インデックス (サーバーサイドキャッシュ等)

現時点では用途未解明。

---

## 参考: ファイルパス

```
/Users/Shared/Scarbee Vintage Keys Library/Samples/
    Scarbee Vintage Keys_Mark I.nkx   (1,345 MB)
    Scarbee Vintage Keys_Mark I.nkc   (117 KB)
    Scarbee Vintage Keys_A-200.nkx    (842 MB)   ← Wurlitzer A-200
    Scarbee Vintage Keys_Clavinet Pianet_001.nkx  (2.0 GB)
    Scarbee Vintage Keys_Clavinet Pianet_002.nkx  (1.8 GB)
```
