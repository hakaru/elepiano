# Claude Worklog 2026-03-07

---
2026-03-07 01:28
作業項目: LA Custom / Vintage Vibe FLAC CRC-16 一括修復
追加機能の説明:
  - extract_samples.py に実装済みの _fix_flac_crc16() を使い、全 FLAC ファイルの CRC-16 を修復
  - 前回 ProcessPoolExecutor でピクリングエラー → ThreadPoolExecutor に切り替え
  - LA Custom attack (2819) + release (1408) + Vintage Vibe EP (1539) を一括修復
決定事項:
  - ThreadPoolExecutor でバッチ修復を実行
  - 修復後 Pi にrsync → キャッシュ削除 → elepiano 再起動
次のTODO: バッチ修復スクリプト実行
---

---
2026-03-07 02:05
作業項目: LA Custom XOR 暗号化修正 + int16 in-memory PCM
追加機能の説明:
  - LA Custom が実は XOR 暗号化されていた（encrypted=False は誤り）
  - フレーム0のみデコード可能（4096サンプル=0.09秒）だった根本原因を解明
  - extract_samples.py: rhodes-la-attack/rhodes-la-rel を encrypted=True に修正
  - 再抽出: attack 2985件、release 1760件 全成功
  - メモリ内 PCM を float → int16 に変更（RAM 50% 削減）
    - sample_db.hpp: SampleData::pcm を vector<int16_t> に変更
    - voice.cpp: int16→float 変換を再生時に実行（kInt16ToFloat = 1/32767）
    - sample_db.cpp: process_sample で float→int16 変換、load_cache で int16 直接読み込み
    - save_cache も int16 直接書き込み（変換不要に）
  - CACHE_VERSION 8→9
決定事項:
  - LA Custom はフレーム1以降が XOR キー [ef 42 12 bc] で暗号化
  - ローテーション検出: フレーム1先頭が ff f8 c9 0c になる rot を選択
  - 再抽出後: dr_flac で全フレーム正常デコード確認（4096→923455サンプル等）
  - 3音色起動成功: used=6.2GB, swap=292MB, available=1.7GB（安定）
  - OOM 回避: int16 in-memory で float 比 50% メモリ削減
次のTODO: PG2 演奏テスト（デジタルノイズ解消確認）、コミット
---

---
2026-03-07 02:09
作業項目: LA Custom ピッチ不安定修正 — SampleDB::find() 等距離ノート選択バグ
追加機能の説明:
  - LA Custom は21ノートのスパースサンプリング（88鍵全てではない）
  - 等距離の2ノートがある場合、両方の候補を混ぜてベロシティマッチしていた
  - 例: note 57 → note 54(距離3) と note 61(距離4) だが、距離が同じ場合は両方を混ぜる
  - ベロシティの微妙な違いで note 54 と note 61 のサンプルが交互に選ばれピッチが不安定に
決定事項:
  - 等距離の場合は低い方のノートを1つだけ選ぶように修正
  - Pi ビルド・デプロイ完了
次のTODO: 演奏テスト
---

---
2026-03-07 02:19
作業項目: LA Custom ピッチ不安定 切り分けテスト
追加機能の説明:
  - samples.json を rr1 のみに絞り込み（2985→1929サンプル）
  - ラウンドロビン混在が原因か切り分け
決定事項:
  - Note 42 は rr1-4 × 88 vel = 352 エントリ、Note 45/54 は rr1 のみ 83 エントリ
  - rr1-4 間でベロシティ範囲が重複しており、find() の結果が不安定な可能性
  - まず rr1 のみでテスト
次のTODO: rr1 のみで演奏テスト → ピッチ安定ならRR混在が原因
---

---
2026-03-07 02:32
作業項目: LA Custom XOR 復号の根本原因修正 — _find_encrypted_frame1() 偽陽性バグ
追加機能の説明:
  - FFT分析で vel045=110Hz(正解), vel060=261Hz(不正解), vel075=622Hz(不正解) を確認
  - 根本原因: _find_encrypted_frame1() が frame0 ヘッダ直後から探索開始するが、
    frame0 のオーディオデータ(~12KB)内で偽陽性マッチを拾っていた
  - 3バイト(sync+byte2)のみのマッチでは偽陽性率が高すぎる
  - 修正: XOR復号後のフレームヘッダ全体の CRC-8 を検証して偽陽性を排除
決定事項:
  - _find_encrypted_frame1() に CRC-8 検証を追加
  - 全 LA Custom サンプルを再抽出
次のTODO: extract_samples.py 修正 → 再抽出 → Pi デプロイ → 演奏テスト
---

---
2026-03-07 02:46
作業項目: LA Custom ピッチ不安定の根本原因修正完了
追加機能の説明:
  - 根本原因: parse_lacrm_name() が MIDI ノートとベロシティインデックスを逆にパースしていた
  - "RR01 lacrm NUM1 NUM2.wav" → NUM1=MIDIノート, NUM2=ベロシティインデックス (FFT検証で確定)
  - 旧パース: midi_note=NUM2, velocity_idx=NUM1 → ベロシティに応じて違うノートのサンプルを選択
  - LA Custom は実は88ノート(21-108)のフルサンプリングだった（21ノートと思っていたのは逆パースのため）
  - CRC-8 検証も _find_encrypted_frame1() に追加（XOR復号の偽陽性防止）
  - parse_lacr_rel_name() も同様に修正
決定事項:
  - parse_lacrm_name/parse_lacr_rel_name のノートとベロシティを入れ替え
  - Attack 2985件 + Release 1760件を再抽出、Pi上でファイルリネーム + samples.json更新
  - 3プログラム全起動成功: used=6.2GB, available=1.6GB, swap=71MB
次のTODO: PG2 演奏テスト → ピッチ安定確認 → コミット
---

---
2026-03-07 03:07
作業項目: LA Custom 低ベロシティ音抜け修正 — XOR CRC-8 偽陽性対策
追加機能の説明:
  - 1929件中13件が frame 0 (4096サンプル=0.09秒) しかデコードできず、低ベロシティで音が抜けていた
  - 根本原因: _find_encrypted_frame1() の CRC-8 検証が偽陽性を返すケース
    - CRC-8 (1/256 偽陽性率) と CRC-16 (フレーム全体) の両方が一致する稀なケース
    - XOR キーの4バイト周期性のため、次フレーム検証も通過
  - 修正: _find_encrypted_frame1() を複数候補を返す方式に変更
    - decode_spca() で各候補を dr_flac で検証し、frame 0 以上デコードできるものを採用
  - 結果: 1929/1929 全ファイルが正常デコード (13件修正, 2969→2969サンプル)
決定事項:
  - _find_encrypted_frame1() の戻り値を tuple → list[tuple] に変更
  - decode_spca() に _drflac_quick_check() 検証ループを追加
  - Pi デプロイ完了: 2969 attack + 1749 release + 他音色 正常起動
次のTODO: PG2 低ベロシティ演奏テスト → コミット
---

---
2026-03-07 10:40
作業項目: Raspberry Pi 8GB メモリ効率運用のドキュメント監査
追加機能の説明:
  - docs/ と src/ を横断確認し、Pi運用のメモリ制約・起動時間・キャッシュ運用を整理
  - 実装とドキュメントの整合を確認（SampleDB キャッシュ、MAX_SAMPLE_FRAMES、並列デコード）
決定事項:
  - `.pcm_cache` は実装済みでウォーム起動に有効
  - 20秒サンプル長・高並列デコードはPiでメモリピーク増大要因
次のTODO: 改善候補をIssue化
---

---
2026-03-07 11:02
作業項目: GitHub Issue 起票（性能・RT・保守）
追加機能の説明:
  - 監査結果をGitHubへ起票
決定事項:
  - #31 performance: shrink .pcm_cache by storing PCM as int16 instead of float32
  - #32 race: parallel SampleDB decode uses std::vector<bool> completion flags
  - #33 performance: make MAX_SAMPLE_FRAMES configurable and safer by default on Pi
  - #34 performance: cap/configure parallel FLAC decode thread count
  - #35 refactor: unify duplicated engine run loops and detect worker thread failure
  - #36 BLE status: disconnected state is not emitted after monitor disconnect
  - #37 security: command characteristic allows unauthenticated service restarts
  - #38 BLE MIDI: reconnect ALSA destination after elepiano restart/client-id change
次のTODO: テスト追加と機能拡張作業
---

---
2026-03-07 11:30
作業項目: テスト追加（StatusMonitor）
追加機能の説明:
  - `tests/test_status_monitor.py` を新規作成
  - カバー範囲:
    - connect失敗時の戻り値
    - run中のJSON受信とcallback通知
    - 切断時のreader/writerリセット
    - 既知課題（切断時 stale status）を expectedFailure で回帰テスト化
決定事項:
  - `python3 -m unittest -v tests/test_extract_samples.py tests/test_status_monitor.py`
  - 結果: `OK (expected failures=2)`
次のTODO: FXChainへのLV2ホスト追加
---

---
2026-03-07 12:10
作業項目: FXChain にオプションLV2ホスト機能を追加
追加機能の説明:
  - CMakeオプション追加: `ELEPIANO_ENABLE_LV2`（デフォルトOFF）
  - `lilv-0` ベースの最小LV2ホストを新規実装
    - `src/lv2_host.hpp`
    - `src/lv2_host.cpp`
  - `FxChain` に統合:
    - 有効時のみ初期化（`ELEPIANO_LV2_URI` 環境変数で指定）
    - FX後段でLV2処理を適用
    - `ELEPIANO_LV2_CC_MAP` でCC→control portマップを適用
  - 既定OFFのため既存動作は維持
決定事項:
  - 反映ファイル:
    - `CMakeLists.txt`
    - `src/fx_chain.hpp`
    - `src/fx_chain.cpp`
    - `src/lv2_host.hpp`
    - `src/lv2_host.cpp`
次のTODO: commit & push
---

---
2026-03-07 12:24
作業項目: 変更をコミット・push
追加機能の説明:
  - 上記LV2ホスト追加 + StatusMonitorテストをコミット
決定事項:
  - commit: `fbfee05`
  - message: `feat: add optional LV2 host in FxChain and status monitor tests`
  - push: `origin/main` へ反映完了
  - 注記: BLE/iOS/サンプル関連の既存ローカル変更はコミット対象外で保持
次のTODO: 実機（Pi5）でLV2プラグインURI指定の動作確認
---

---
2026-03-07 03:12
作業項目: LV2ホスト機能 コードレビュー
追加機能の説明:
  - lv2_host.hpp/cpp, fx_chain.hpp/cpp, CMakeLists.txt のコードレビュー実施
  - lilv API 使用方法、スレッド安全性、リソース管理、エラーハンドリングを検証
決定事項:
  - lilv_plugin_has_port_with_class() の API 使用法が不正 → lilv_port_is_a() に修正必要
  - set_cc() のスレッド安全性: MIDI→audio スレッド間で std::atomic 不使用（実用上は許容範囲）
  - Impl を raw pointer で管理 → unique_ptr 推奨
  - control_values を num_ports サイズで確保（軽微な無駄だが問題なし）
次のTODO: レビュー指摘事項の修正
---

---
2026-03-07 03:12
作業項目: GitHub Issue 棚卸し
追加機能の説明:
  - 全 open issue (#26,30,32-38) の現状を確認
  - #31 (pcm_cache int16) は本日の作業で実装済み → CLOSED 確認
決定事項:
  - 解決済み: #31 (int16 cache) — 本日コミット済み
  - 解決済みと思われる: #32 (vector<bool> race) — 要コード確認
  - 残 issue 整理:
    - 性能系: #33 (MAX_SAMPLE_FRAMES), #34 (thread count cap) — Pi安定性に影響
    - RT安全性: #26 (WAV dump fwrite) — デバッグ機能のみ、優先度低
    - コード品質: #30 (sanitize_flac), #35 (run loop 統合)
    - BLE系: #36 (切断通知), #37 (認証), #38 (MIDI再接続) — BLEデプロイ時に対応
次のTODO: #32 のコード確認、LV2 API修正
---

---
2026-03-07 03:16
作業項目: GitHub Issue #32 コード調査 — vector<bool> race condition 確認
追加機能の説明:
  - src/sample_db.cpp の並列 FLAC デコード部分を確認
  - Issue は依然存在（修正されていない）
  - L293: `std::vector<bool> ok(n, false);` で completion フラグ管理
  - L301-303: 複数worker がレース無しで ok[i] にアクセス（実装は安全に見えるが設計的に脆弱）
決定事項:
  - Issue はまだ存在: std::vector<bool> は bitwise packing のため、複数スレッドが同じバイトを読み書きする可能性
  - 当該箇所: worker lambda が ok[i] = process_sample(tasks[i]) を実行
  - 各スレッドが異なる i にアクセスするため実際のレース可能性は低いが、メモリモデル上は未定義動作の可能性
  - 最小限の修正: vector<bool> を vector<uint8_t> か vector<std::atomic<bool>> に変更
次のTODO: 修正実装（Plan モード）
---
