Checking build status...
Build is up-to-date.
(node:66760) Warning: `--localstorage-file` was provided without a valid path
(Use `node --trace-warnings ...` to show where the warning was created)
YOLO mode is enabled. All tool calls will be automatically approved.
YOLO mode is enabled. All tool calls will be automatically approved.
I will begin by reading the `GEMINI.md` file and checking the current status of the project to provide a comprehensive code review.

I will list the contents of the `docs/` directory to find project context and run the command to check GitHub issues.

I will check the latest work log in the `docs/` directory and then review the current changes using `git diff`.

I will read the source code files for the sample database, FLAC decoder, and synth engine to check for any potential risks or issues.

I will check the git log and the content of `src/flac_decoder.cpp` to verify if the changes mentioned in the worklog have been committed.

I will perform a final `git diff` check to ensure all unstaged changes are identified and then categorize my findings by importance.

プロジェクトの現状および変更内容（staged/unstaged）を、コード品質、リアルタイム性能、バグのリスク、セキュリティの観点からレビューしました。

### レビュー結果要約

| カテゴリ | 項目 | 重要度 | 内容 |
| :--- | :--- | :--- | :--- |
| **リアルタイム性** | `SampleDB::find()` でのメモリ再確保 | **Critical** | オーディオスレッドから呼ばれる `push_back` が再確保を誘発し、音切れの原因となる。 |
| **リアルタイム性** | 同期 I/O および RT スケジューリング | **High** | 未ステージングの変更で `fprintf` の排除と `SCHED_FIFO` が導入されており、大幅に改善。 |
| **バグ・性能** | FLAC デコーダの全ファイル走査 | **Medium** | 起動時に全サンプルの全バイトを走査するため、巨大なサンプルセットで起動が遅延する。 |
| **セキュリティ** | パストラバーサル対策 | **Medium** | `weakly_canonical` によるガードがあるが、さらに堅牢な検証（Issue #20）が推奨される。 |
| **品質・安全** | PCM 健全性チェックとソフトクリップ | **Good** | 異常値検知と多項式ソフトクリップにより、スピーカー損傷や不快なノイズが防止されている。 |

---

### 詳細分析

#### 1. リアルタイム性能の安全性 (重要度: **Critical**)
*   **場所:** `src/sample_db.cpp` の `SampleDB::find()`
*   **詳細:** オーディオスレッド（`SynthEngine::mix`）から直接呼ばれるこの関数内で、メンバ変数 `candidates_` に対して `push_back()` を実行しています。
*   **リスク:** コンストラクタで `reserve(16)` されていますが、ベロシティレイヤーやラウンドロビンが 16 を超えるノートがあった場合、**オーディオスレッド内でヒープの再確保 (realloc)** が発生します。これは非決定的な遅延を生み、オーディオドロップアウト（プチノイズ）の直接的な原因になります。
*   **対策:** `candidates_` を十分なサイズ（例: 64）で固定確保するか、オーディオスレッド外で事前計算されたインデックスを参照するように修正が必要です。

#### 2. 同期 I/O の排除とスケジューリング (重要度: **High / 改善済み**)
*   **場所:** `src/main.cpp`, `src/audio_output.cpp` (未ステージングの変更)
*   **詳細:** MIDI ログ出力を `SpscQueue` 経由でメインスレッドに委譲し、アンダーラン警告の `fprintf` を atomic カウンタに置き換えています。また、`SCHED_FIFO` (優先度 80) の設定も追加されています。
*   **評価:** これらはリアルタイム性能を確保するために極めて重要な変更であり、Issue #25 で懸念されていたデジタルノイズの多くを解消するはずです。早期のコミットを推奨します。

#### 3. FLAC デコーダの起動性能 (重要度: **Medium**)
*   **場所:** `src/flac_decoder.cpp`
*   **詳細:** CRC-16 検証のため、`read_file_bytes` でファイル全体をメモリに読み込み、`find_frame_starts` で全バイトを走査しています。
*   **リスク:** 64MB の制限はあるものの、1600 ファイル近いサンプルを読み込む際、$O(N)$ の走査が繰り返されるため、Raspberry Pi 5 の I/O パフォーマンスによっては起動時間が数分単位で延びる可能性があります。
*   **対策:** CRC 検証をオンデマンドにするか、ファイルヘッダ付近のみを走査するなどの最適化が検討の余地があります。

#### 4. セキュリティ: パストラバーサル (重要度: **Medium**)
*   **場所:** `src/sample_db.cpp`
*   **詳細:** `std::filesystem::weakly_canonical` を使用したチェックが実装されています。
*   **リスク:** シンボリックリンクを悪用した攻撃に対して、`relative()` の結果だけでは不十分な場合があります。
*   **対策:** 正規化されたパスが、期待されるベースディレクトリの文字列で開始されているかを明示的に検証するコードを追加することで、Issue #20 に対応できます。

#### 5. PCM 健全性チェックとソフトクリップ (評価: **Good**)
*   **詳細:** 異常値検知 (±2.0超)、DC オフセット検知、および `SynthEngine::mix` での 3 次多項式によるソフトクリップ (`x * (1.5 - 0.5 * x^2)`) は、非常に実用的かつ効率的な実装です。これにより、過大入力によるハードクリップノイズを抑えつつ、聴感上の音量を維持できています。

---

総評として、現在未ステージングの変更はリアルタイム性能を劇的に向上させるものであり、非常に高く評価できます。唯一の重大な懸念点である `SampleDB::find()` 内のメモリ再確保リスクを修正すれば、プロダクションレベルのリアルタイム安全性が確保されます。
