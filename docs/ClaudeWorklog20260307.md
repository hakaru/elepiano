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

---
2026-03-07 03:20
作業項目: LV2 修正 + Issue #32/#33/#34 一括対応
追加機能の説明:
  - Task A: LV2 lilv API 修正 — lilv_plugin_has_port_with_class() → lilv_port_is_a() に置換、Impl を unique_ptr 化
  - Task B: #32 vector<bool> race 修正 — vector<uint8_t> に変更
  - Task C: #33/#34 環境変数で MAX_SAMPLE_FRAMES とスレッド数を設定可能に
決定事項:
  - Task A は Agent に委譲、Task B+C はメインで順次実行
  - 全て src/ 内の変更で独立
次のTODO: 実装 → LV2 OFF ビルド検証
---

---
2026-03-07 03:21
作業項目: LV2 lilv API 修正 + Impl unique_ptr 化
追加機能の説明:
  - lilv_plugin_has_port_with_class() → lilv_port_is_a() に置換（正しい API 使用法）
  - Impl を raw pointer → std::unique_ptr<Impl> に変更
  - デストラクタを default 化（unique_ptr が自動解放）
  - Impl に ~Impl() デストラクタ追加（shutdown() を呼ぶ）
決定事項:
  - lv2_host.hpp: memory ヘッダ追加、unique_ptr 宣言
  - lv2_host.cpp: ポート検出ループ修正、コンストラクタ/デストラクタ修正
次のTODO: ビルド検証
---

---
2026-03-07 03:43
作業項目: FxChain Overdrive → LV2チェーン + 内蔵IR畳み込み置換
追加機能の説明:
  - IrConvolver 新規クラス: overlap-save ブロック畳み込み（WAV IR ファイル対応）
  - Lv2Host マルチインスタンス対応: LilvWorld 共有コンストラクタ + initialize() メソッド追加
  - FxChain 統合: LV2チェーン + IR有効時は内蔵preamp/cabinetをスキップ
  - 環境変数: ELEPIANO_LV2_CHAIN, ELEPIANO_LV2_N_CC_MAP/WET, ELEPIANO_IR_FILE/WET
  - 後方互換: 従来の ELEPIANO_LV2_URI も動作
決定事項:
  - IR は LV2 ではなく内蔵畳み込みで実装（LV2 Atom/Worker/State 回避）
  - Twin Reverb cab IR は 512-2048 サンプル → Pi5 で 0.5ms 未満
  - シグナルフロー: tremolo → [LV2 chain] → [IR] → EQ → chorus → space
次のTODO: 実装 → LV2 OFF ビルド検証
---

---
2026-03-07 07:55
作業項目: LV2チェーン + IR畳み込み — Pi5 ビルド & 動作検証
追加機能の説明:
  - ir_convolver.cpp に cstdint include 追加（GCC 14 で必要）
  - LV2 ON/OFF 両方のビルド成功
  - gxts9 ポート番号確定: 0=Level(-20..4), 1=Tone(100..1000), 2=Drive(0..1), 5=BYPASS
  - set_cc() に port range スケーリング追加（normalized 0-1 → min..max）
  - control port のデフォルト値を lilv_port_get_range() で取得・初期化
決定事項:
  - CC70=Drive(port 2), CC71=Tone(port 1) のマッピングで動作確認
  - gxts9 は mono (1 audio in, 1 audio out) → L/R 同じポートにフォールバック
  - Pi5 で LV2 chain 初期化成功: "[FxChain] LV2 chain active (1 plugins)"
  - guitarix-lv2, liblilv-dev をPiにインストール済み
次のTODO: Twin Reverb IR wav を用意してIR畳み込みテスト、実演奏テスト
---

---
2026-03-07 07:57
作業項目: Twin Reverb cab IR 入手 + IR 畳み込みテスト
追加機能の説明:
  - Shift Line "Fender Twin 73 IR Pack" (無料) をダウンロード
  - 10種のマイク位置バリエーション (24bit/mono/48kHz, 各1000サンプル=21ms)
  - data/ir/ に配置、Pi に転送
  - Pi5 で IR + LV2 チェーン同時動作確認:
    - "[IR] loaded: 09_Twin73_cab_plusback_L19.wav (1000 samples, 48000 Hz, 24-bit)"
    - "[FxChain] IR convolver enabled (1000 samples)"
    - "[FxChain] LV2 chain active (1 plugins), internal preamp/cabinet bypassed"
決定事項:
  - 推奨 IR: 09_Twin73_cab_plusback_L19.wav (背面マイク混合、最も自然なキャビネット感)
  - IR は 1000 サンプル → direct convolution で Pi5 負荷は極小
  - 全コンポーネント（LV2 gxts9 + IR convolver + FxChain 統合）動作確認完了
次のTODO: elepiano.service に環境変数を追加して実演奏テスト、コミット
---

---
2026-03-07 08:03
作業項目: elepiano.service に LV2+IR 環境変数を設定、サービス起動テスト
追加機能の説明:
  - /etc/systemd/system/elepiano.service に環境変数追加:
    - ELEPIANO_LV2_CHAIN, ELEPIANO_LV2_0_CC_MAP, ELEPIANO_LV2_0_WET
    - ELEPIANO_IR_FILE, ELEPIANO_IR_WET
  - 3音色プログラム (rhodes-classic + LA custom + vintage-vibe) + LV2 gxts9 + IR 同時起動成功
  - 全サンプル読み込み完了後、LV2/IR 初期化成功
決定事項:
  - サービスとして LV2 + IR 常時有効で運用開始
  - underrun は起動直後の1回のみ（period=32, periods=2 の1.3ms設定で安定）
  - MIDI ノート受信・演奏動作確認済み
次のTODO: 演奏テスト（音質確認）、コミット
---

---
2026-03-07 08:31
作業項目: IR リアルタイム ON/OFF + マイク位置切替 (CC制御)
追加機能の説明:
  - IrConvolver: 複数 IR ロード対応 (load_dir)、select(index) でリアルタイム切替
  - FxChain: CC74=IR wet (ON/OFF)、CC69=IR マイク位置選択
  - ELEPIANO_IR_DIR 環境変数でディレクトリ指定 (ELEPIANO_IR_FILE と後方互換)
  - LV2 モノプラグイン修正: L+R→mono入力、mono→両ch出力
  - LV2 port defaults override: ELEPIANO_LV2_0_DEFAULTS 環境変数
決定事項:
  - gxts9 Level=-16dB デフォルトが無音の原因 → override 0dB で解決
  - mono plugin connect_port 二重呼び出し → out_l_buf がゼロ → 修正済み
  - Studio Preamp / GxBooster も動作確認済み
次のTODO: IrConvolver マルチIR 実装
---

---
2026-03-07 08:34
作業項目: FxChain CC74/CC69 IR制御 + IrConvolver API修正
追加機能の説明:
  - FxChain::apply_param() に CC74 (IR wet ON/OFF) と CC69 (IR マイク位置選択) を追加
  - コンストラクタの ir_conv_->ir_length() を新API ir_count() に修正（コンパイルエラー修正）
  - ELEPIANO_IR_DIR 対応: initialize_from_env() が自動でディレクトリ優先ロード
  - IR ロード後のログにIR数とアクティブIR名を表示
決定事項:
  - CC74: 0=OFF(wet=0), 1-127=ON(wet=ELEPIANO_IR_WET or 0.7)
  - CC69: 0-127 → IR インデックスにスケール (val * ir_count / 128)
  - apply_param() は LV2 チェーン有無に関わらず IR を制御可能
次のTODO: Pi ビルド・デプロイ・テスト
---

---
2026-03-07 08:47
作業項目: Guitarix → Airwindows Channel9 (Neve) に LV2 プラグイン入れ替え
追加機能の説明:
  - Pi5 に airwindows-lv2 をソースビルド・インストール (meson, 170プラグイン)
  - Channel9 プラグイン: Console Type 0=Neve, 1=API, 2=SSL, 3=Teac, 4=Mackie
  - ステレオ in/out、hardRTCapable、float control ports のみ
  - ポート: 4=ConsoleType(0-5), 5=Drive(0-200), 6=Output(0-1)
  - CC70→Drive, CC71→Output で MIDI 制御可能
  - LV2_PATH を systemd service に追加 (/usr/local/lib/aarch64-linux-gnu/lv2)
  - cmake -DELEPIANO_ENABLE_LV2=ON で再ビルド（キャッシュが OFF だったため）
決定事項:
  - Guitarix GxBooster を完全に置換
  - Channel9 Neve モード: Drive=0 (クリーン)、Output=1.0
  - IR 10本 + Channel9 Neve + 3音色プログラム 全て起動成功
次のTODO: 演奏テスト（Neve サウンド確認）
---

---
2026-03-07 09:22
作業項目: Airwindows LV2 チェーン構成検討・試行
追加機能の説明:
  - airwindows-lv2 をPi5にソースビルド・インストール (170プラグイン)
  - Channel9 (Neve) + EQ (1073風) の2段チェーン構成で動作確認
  - Tube2, Vibrato, ToTape6, kPlateB のポート調査
  - LSP plugins インストール → IR Loader は urid/worker/state 必須で非対応
  - LSP Auto Panner は存在しない
  - 全段 Airwindows 置換は断念、現状構成を維持
決定事項:
  - Channel9 Drive=0 は無音になる → Drive=100 で動作
  - period=32 では Channel9 で underrun 多発 → period=64 periods=3 に変更
  - 最終構成: Tremolo → Channel9 (Neve) → EQ → 内蔵IR → 内蔵Chorus → 内蔵Space
  - CC72=EQ Bass, CC73=EQ Treble は前段 Airwindows EQ に割当
次のTODO: FxChain pre/post LV2 チェーン改修
---

---
2026-03-07 09:38
作業項目: FxChain pre/post LV2 チェーン改修 + 全段 Airwindows 化
追加機能の説明:
  - FxChain process() を IR 前後で分割: lv2_pre_chain_ / lv2_post_chain_
  - ELEPIANO_LV2_POST_CHAIN 環境変数追加（セミコロン区切り URI）
  - post chain 有効時は内蔵 EQ/Chorus/Space をバイパス
  - グローバルインデックスで per-plugin 環境変数を統一管理
  - 5段 LV2 構成:
    - Pre[0] Channel9 (Neve): CC68=type, CC70=drive, CC71=output
    - Pre[1] EQ (1073風): CC72=bass, CC73=treble
    - Post[0] Vibrato: CC79=speed, CC80=depth, depth=0(OFF)
    - Post[1] ToTape6: テープ色（Soften=0.3, HeadBump=0.3, Flutter=0.3）
    - Post[2] kPlateB: CC78=wetness, damping=3, wetness=0.15
決定事項:
  - period=64 periods=3 で 5 LV2 プラグイン + IR 同時動作 underrun なし
  - LSP plugins は urid/worker/state 必須で現行ホスト非対応
  - fx_chain.hpp: lv2_chain_ → lv2_pre_chain_ + lv2_post_chain_ に分割
次のTODO: 演奏テスト、音質確認
---

---
2026-03-07 09:51
作業項目: ペダルリリース・共鳴・クリック無効化
追加機能の説明:
  - _note_off(): リリースサンプル起動 (_start_release_voice) を削除
  - CC64 ペダルアップ: リリースサンプル起動 + 共鳴リリース + クリック音を削除
  - CC64 ペダルダウン: 弦共鳴起動を削除
  - process(): pedal_resonance_.mix() / pedal_click_.mix() をコメントアウト
  - サステインペダルの基本機能（ノート保持/解放）は維持
決定事項:
  - ペダル共鳴/クリックはコメントアウト（将来再有効化可能）
  - リリースサンプルは使用しない
次のTODO: 演奏テスト
---

---
2026-03-07 10:03
作業項目: 未使用CC・内蔵エフェクト削除
追加機能の説明:
  - synth_engine.cpp: CC82-87 (ペダル共鳴/クリック制御) 削除
  - fx_chain.cpp: CC70/71 (内蔵Drive/Tone), CC72/73 (内蔵EQ) の apply_param 削除
  - これらの CC は LV2 チェーンのみで処理される
決定事項:
  - 内蔵 preamp/cabinet/EQ のコードはまだ残存（フォールバック用）
  - CC ハンドラのみ削除
次のTODO: 演奏テスト
---

---
2026-03-07 10:12
作業項目: セッション再開 — 現状確認
追加機能の説明:
  - 前回の最終状態: Channel9 Drive=0→無音問題を Drive=1.0 に修正済み
  - elepiano.service 稼働確認: active (running), MIDI 受信中
  - 現在の LV2 構成:
    - Pre[0] Channel9 Neve: Drive=1.0, Output=0.2
    - Pre[1] EQ: CC72=bass, CC73=treble
    - Post[2] Vibrato: Speed=0.3, Depth=0 (OFF)
    - Post[3] ToTape6: Soften=0.3, HeadBump=0.3, Flutter=0.3, Wet=0.5
    - Post[4] kPlateB: Damping=3.0, Wetness=0.02
  - period=64, periods=3 (4ms latency)
決定事項:
  - サービスは正常動作中、音が出る状態
次のTODO: ユーザーの次の指示を待つ、コミット
---

---
2026-03-07 10:14
作業項目: ToTape6 CC番号割り当て
追加機能の説明:
  - ToTape6 ポート確認: Input(4), Soften(5), HeadBump(6), Flutter(7), Output(8), Dry/Wet(9)
  - CC割り当て案: CC75=Soften, CC76=HeadBump, CC77=Flutter, CC85=Dry/Wet
決定事項:
  - ユーザー確認待ち
次のTODO: 承認後に elepiano.service の ELEPIANO_LV2_3_CC_MAP を更新
---

---
2026-03-07 10:41
作業項目: Vibrato→ChorusEnsemble, ToTape6→TapeDelay2 プラグイン入れ替え
追加機能の説明:
  - 本家 Airwindows に ChorusEnsemble (3パラメータ) と TapeDelay2 (6パラメータ) が存在
  - airwindows-lv2 には未移植 → LV2 ラッパーを自前で作成
  - ChorusEnsemble.c/ttl: 4voice コーラスアンサンブル (Speed, Range, Dry/Wet)
  - TapeDelay2.c/ttl: テープエコー (Time, Regen, Freq, Reso, Flutter, Dry/Wet)
  - meson.build + manifest.ttl.in に追加、Pi5 でビルド・インストール成功
  - elepiano.service 更新:
    - Post[0] ChorusEnsemble: CC77=Speed, CC78=Range, Dry/Wet=0 (OFF)
    - Post[1] TapeDelay2: CC79=Time, CC80=Regen, CC81=Freq, CC82=Reso, CC83=Flutter, CC84=Dry/Wet, Dry/Wet=0 (OFF)
    - Post[2] kPlateB: CC85=Wetness
  - 全5 LV2 プラグイン + IR 正常起動確認
決定事項:
  - ChorusEnsemble/TapeDelay2 はデフォルト Dry/Wet=0 (OFF、CCで有効化)
  - kPlateB の CC を 78→85 に移動
次のTODO: 演奏テスト、CC番号整理表の最終化
---

---
2026-03-07 10:56
作業項目: GalacticVibe LV2 追加 + Tremolo/Vibe CC切替
追加機能の説明:
  - GalacticVibe (Uni-Vibe風フェイザー) を airwindows-lv2 に追加ビルド
  - パラメータ: Drift (速度), Dry/Wet — デフォルト OFF (wet=0)
  - FxChain に ModMode 排他切替 (TREMOLO / GALACTIC_VIBE) を実装
    - CC3: 0-63=Tremolo, 64-127=GalacticVibe
    - process() で mod_mode に応じて内蔵Tremolo か LV2 pre[0] GalacticVibe を実行
    - pre chain の残り (Channel9, EQ) は常に実行
  - elepiano.service 更新: GalacticVibe を pre chain 先頭に追加、全インデックス+1
  - 6 LV2 プラグイン (pre 3 + post 3) + IR 正常起動確認
決定事項:
  - GalacticVibe CC: CC1=Drift, CC2=Dry/Wet (Tremolo と同じ CC で自然に操作)
  - CC3 で Tremolo ↔ GalacticVibe 切替
次のTODO: 演奏テスト
---

---
2026-03-07 11:06
作業項目: Small Stone フェイザー内蔵実装（LV2 GalacticVibe 置換）
追加機能の説明:
  - EH Small Stone 風 4段オールパスフェイザーを FxChain に内蔵実装
  - LFO sin wave → 指数スケール 200Hz〜4000Hz スイープ
  - L/R 90° LFO 位相差でステレオ化
  - Feedback パスでレゾナンス（Color）
  - CC1=Rate, CC2=Depth (Tremolo と共有)、CC3 で切替
  - GalacticVibe LV2 を置換、LV2 pre chain から除外
決定事項:
  - ModMode::GALACTIC_VIBE → ModMode::PHASER にリネーム
  - process_phaser() を ~40行で実装
  - デフォルト mod_mode を TREMOLO に戻す
次のTODO: 実装 → ビルド検証
---

---
2026-03-07 11:50
作業項目: CC3 切替不具合修正 + iOS UI追加
追加機能の説明:
  - 問題1: iOS CCParameter.swift に CC3 (Mod切替) が未登録 → 切替UI追加
  - 問題2: CC1共有で Depth=0 だと Phaser 効果ゼロ → Tremolo グループを Modulation にリネーム、CC3 をトグルスイッチで追加
決定事項:
  - CCGroup に .modulation を追加、CC3 (Tremolo/Phaser 切替) を追加
  - CC1/CC2 は Modulation グループに移動
  - Tremolo/Overdrive/PedalResonance/PedalClick グループ整理
次のTODO: iOS UI修正 → 動作確認
---

---
2026-03-07 11:56
作業項目: CC3切替不具合再調査 — 「変わらない」
追加機能の説明:
  - 原因推定: LV2 pre chain ループを i=1→全プラグインに変更したため、
    GalacticVibe が elepiano.service のチェーンに残っていると常時実行される
  - 対策: elepiano.service から GalacticVibe URIを除外し、LV2インデックスを-1する必要
  - Pi SSH不可のため、rsync + rebuild 手順を出力
決定事項:
  - ユーザーに Pi デプロイ手順を提示
次のTODO: Pi デプロイ → リビルド → テスト
---

---
2026-03-07 12:02
作業項目: Phaser Color (CC4) パラメータ追加
追加機能の説明:
  - CC4 = Phaser Color（feedback/resonance, 0..0.9）
  - fx_chain.cpp apply_param に case 4 追加
  - iOS CCParameter.swift に CC4 "Color" 追加（デフォルト71≈0.5）
  - Pi デプロイ・リビルド・再起動完了
決定事項:
  - CC4 は Phaser 専用（Tremolo モードでは無効だが値は保持）
次のTODO: 演奏テスト
---

---
2026-03-07 12:08
作業項目: GitHub issue 棚卸し（10件オープン）
追加機能の説明:
  - 全 open issue を再確認（#26,30,35-42）
決定事項:
  - #37 と #40 は重複（BLE コマンド認証）→ 1つクローズ候補
  - #39 は意図的無効化の記録 → クローズ候補
  - #42 lilv_world nullptr ガード → コード確認で既に if 文あり
  - #41 IR O(N*M) → 現行 IR 1000サンプルで問題なし
次のTODO: ユーザー指示待ち
---

---
2026-03-07 12:11
作業項目: GitHub Issue 一括整理（クローズ4件）
追加機能の説明:
  - #42 lilv_world nullptr — fx_chain.cpp L78 に if ガード済み → クローズ
  - #39 ペダル共鳴/クリック — synth_engine.cpp でコメントアウト済み → クローズ
  - #35 run ループ重複 — run_engine() に統合済み → クローズ
  - #37 BLE コマンド認証 — #40 と重複 → クローズ
決定事項:
  - 4件クローズ後、オープン issue は6件に減る
次のTODO: gh issue close 実行
---

---
2026-03-07 12:12
作業項目: 新規 issue #43/#44 確認 + #44 クローズ + #43 対応 + ピッチベンド実装
追加機能の説明:
  - #44 は #40 と完全重複（BLE コマンド認証なし）→ クローズ
  - #43 __pycache__ → .gitignore に追加で解決
  - ピッチベンド未実装を発見 → 4ファイル修正で実装
    - midi_input.hpp: MidiEvent::Type::PITCH_BEND 追加
    - midi_input.cpp: SND_SEQ_EVENT_PITCHBEND ハンドラ追加
    - synth_engine.hpp/cpp: bend_ratio_ + _pitch_bend() 追加
    - voice.hpp/cpp: mix() に bend_ratio 引数追加、position 進行に乗算
決定事項:
  - #44 をクローズ、#43 は .gitignore 修正
  - ピッチベンド範囲: ±2半音（標準）
  - bend_ratio_ は全 Voice に一括適用
次のTODO: Pi ビルド・デプロイ・テスト
---

---
2026-03-07 12:28
作業項目: PG4 にオルガンモードを統合
追加機能の説明:
  - main.cpp の run_piano() に OrganEngine を統合
  - --pg --organ で PG4 としてオルガンを登録可能に
  - プログラムチェンジで SynthEngine ↔ OrganEngine をリアルタイム切替
  - MIDI/Audio コールバックで active engine に応じてルーティング
決定事項:
  - OrganEngine はサンプル不要なので SampleDB は使わない
  - FxChain はオルガンにも適用
次のTODO: 実装
---

---
2026-03-07 12:32
作業項目: setBfree LV2 プラグイン調査（コード変更なし）
追加機能の説明:
  - setBfree GitHub リポジトリ (pantherb/setBfree) の LV2 構成を詳細調査
  - b_synth.ttl.in / lv2.c / midi.c を直接読み込んで確認
  - ポート構成、MIDI CC マッピング、ビルド手順、RT 対応を確認
決定事項:
  - URI: http://gareus.org/oss/lv2/b_synth
  - ポート: 0=Atom MIDI In, 1=Atom MIDI Out, 2=Audio OutL, 3=Audio OutR（制御ポートなし）
  - 全パラメータは Atom port 経由の MIDI CC で制御（LV2 control port は存在しない）
  - hardRTCapable: yes（optionalFeature）
  - requiredFeature: urid:map, work:schedule → 現行 elepiano LV2 ホストでは非対応
  - ドローバー: CC70-78 (upper/lower/pedal 共通、MIDI ch で区別)
  - Leslie: CC1=speed-preset, CC64=speed-toggle
  - Vibrato: CC92=knob, CC95=routing, CC30=lower, CC31=upper
  - Percussion: CC80=enable, CC81=volume, CC82=decay, CC83=harmonic
  - Overdrive: CC65=enable, CC93=character
  - Reverb: CC91=mix
  - ビルド: make && sudo make install（ARM 固有設定なし、OPTIMIZATIONS 変数で調整可能）
次のTODO: elepiano LV2 ホストに work:schedule / urid:map 対応を追加するか、代替案を検討
---

---
2026-03-07 13:06
作業項目: setBfree 内部 API 調査 — ライブラリ直接リンク可能性
追加機能の説明:
  - setBfree GitHub リポジトリのソースコードを詳細調査（コード変更なし）
  - b_synth/lv2.c の instantiate()/run() から内部 API 構造を解析
  - tonegen.h, midi.h, whirl.h, overdrive.h のシグネチャ確認
  - LV2 レイヤーを介さない最小統合フローの設計
  - ビルド構成・静的ライブラリ化の可能性を確認
決定事項:
  - b_instance 構造体が全コンポーネントの中心（tonegen/whirl/preamp/reverb/midicfg/state/progs）
  - allocSynth() → initSynth(rate) → parse_raw_midi_data() → oscGenerateFragment() → preamp() → whirlProc() → reverb() が最小フロー
  - LV2 レイヤーは B3S 構造体 + Atom/Worker/State で囲んでいるだけ、内部 API は完全に独立
  - 各コンポーネントは void* ベースの C API、ヘッダのみで利用可能
  - 静的ライブラリ化は容易（各ディレクトリの .c を直接コンパイル）
  - 外部依存: libm, libpthread のみ（JACK/ALSA/LV2 は不要）
  - BUFFER_SIZE_SAMPLES=128 固定、oscGenerateFragment() は 128 サンプル単位
次のTODO: elepiano への統合設計（CMake サブプロジェクト or ソース直接取り込み）
---

---
2026-03-07 13:06
作業項目: setBfree 統合 — 実装計画
追加機能の説明:
  - setBfree を git submodule として取り込み、静的ライブラリ化
  - setBfreeEngine クラス（C++ ラッパー）を新規作成
  - PG4 の OrganEngine を setBfreeEngine に置換
決定事項:
  - 方針B: ライブラリ直接リンク
  - setBfree 14ファイルを静的ライブラリとしてビルド
  - parse_raw_midi_data() で MIDI を渡し、oscGenerateFragment→preamp→reverb→whirlProc3 で音声取得
  - 128サンプル固定ブロック → elepiano 側でバッファリング
次のTODO: git submodule add → CMakeLists.txt → setBfreeEngine クラス実装
---

---
2026-03-07 13:18
作業項目: VB3-II + yabridge の実現可能性検討
追加機能の説明:
  - GSi VB3-II (Windows VST) を yabridge 経由で Pi5 上で動作させたいリクエスト
決定事項:
  - Pi5 は aarch64 (ARM) → Wine は x86 エミュレーション (box64) が必要
  - yabridge + Wine + box64 + VB3-II のリアルタイムオーディオは非現実的
  - 代替案を提示
次のTODO: ユーザーに技術的制約を説明
---

---
2026-03-07 13:19
作業項目: setBfree 音質チューニングパラメータ調査
追加機能の説明:
  - tonegen.c oscConfig() の設定パラメータ一覧（キークリック、クロストーク、パーカッション、EQ等）
  - whirl.c Leslie 設定パラメータ（ホーン/ドラム回転速度、マイク距離、フィルタ等）
  - overdrive.c の設定パラメータ
  - reverb.c の設定パラメータ
  - vibrato.c の設定パラメータ
  - 設定ファイルフォーマットと実行時設定の可能性
決定事項: 調査完了、全パラメータ列挙済み（下記回答参照）
次のTODO: VB3-II風チューニング設定ファイル作成、setBfree統合実装
---

---
2026-03-07 13:20
作業項目: setBfree 音質チューニングパラメータ調査結果
追加機能の説明:
  - tonegen.c: 40パラメータ（チューニング、パーカッション、EQ、クロストーク、キークリック）
  - whirl.c: 35パラメータ（ホーン/ドラムRPM、加減速、マイク距離/角度、フィルタ6段）
  - overdrive.c: 8パラメータ（入出力ゲイン、バイアス、フィードバック、パワーサグ）
  - reverb.c: 5パラメータ（wet/dry/mix/input/outputgain）
  - vibrato.c: 4パラメータ（スキャナ周波数、V1/V2/V3変調量）
  - キーコンプレッション: KEYCOMPRESSION マクロで有効（設定パラメータなし、テーブル固定）
  - evaluateConfigKeyValue() で実行時パラメータ変更可能
  - 設定ファイルは key=value 形式（# コメント、config.read で連鎖可能）
決定事項:
  - VB3-II的リッチサウンドに重要なパラメータを特定:
    1. キークリック: osc.attack.click.level (0.5→0.7-0.8), maxlength (0.625→0.8)
    2. クロストーク: osc.compartment-crosstalk (0.01→0.03-0.05)
    3. Leslie: whirl.mic.distance, whirl.horn.mic.angle, horn/drum RPM
    4. オーバードライブ: overdrive.character, xov.ctl_biased_fb/fb2
    5. キーコンプレッション: 常時有効、設定不可（コード変更が必要）
次のTODO: VB3-II風チューニング .cfg ファイル作成
---

---
2026-03-07 13:19
作業項目: setBfree VB3-II風チューニング実装
追加機能の説明:
  - data/organ/vb3-style.cfg チューニング設定ファイル作成
  - SetBfreeEngine に設定ファイル読み込み対応追加
  - evaluateConfigKeyValue() で各パラメータを適用
決定事項:
  - クロストーク増量、キークリック強化、Leslie マイク調整、リバーブ軽め
  - ELEPIANO_ORGAN_CFG 環境変数で設定ファイルパスを指定可能に
次のTODO: 実装 → Pi デプロイ → 演奏テスト
---

---
2026-03-07 13:32
作業項目: VB3-style.cfg と setBfree デフォルトの差分説明
追加機能の説明:
  - ユーザーの「前回とどこが違う？」に回答
  - 旧 OrganEngine（自前トーンホイール合成）vs 新 setBfree（フル物理モデル）の違い
  - vb3-style.cfg のデフォルトからの変更点を列挙
決定事項:
  - 回答のみ、コード変更なし
次のTODO: コミット（setBfree統合 + ピッチベンド + issue整理の全変更）
---

---
2026-03-07 14:35
作業項目: Hammond B3 シリアルコンタクト・スイッチング実装
追加機能の説明:
  - setBfree tonegen にベロシティ連動シリアルコンタクト機能を実装
  - 鍵盤のベロシティに応じてバス0-8を時間差でアクティベート
  - 速く叩く→全接点同時（パチッ）、ゆっくり→バラバラ（クシュクシュ）
  - 変更ファイル: tonegen.h, tonegen.c, midi.c, vb3-style.cfg
決定事項:
  - vendor/setBfree ソースを直接修正
  - pendingContacts キュー方式で遅延バスアクティベーション
  - 既存 attackEnv（クリック音）が各接点クロージャで自然に発生
次のTODO: 実装、ビルド、テスト
---

---
2026-03-07 14:42
作業項目: シリアルコンタクト Pi デプロイ確認
追加機能の説明:
  - ユーザーからデプロイ状況の確認依頼
  - Pi にビルド・デプロイを試行
決定事項:
  - 確認中
次のTODO: Pi ビルド → デプロイ → テスト
---

---
2026-03-07 15:00
作業項目: VB3-II リアリズム機能拡張プラン策定
追加機能の説明:
  - フリーランニング・トーンホイール特性の強化
  - Generator Leakage / Resistor Wires / Crosstalk の CC 制御
決定事項:
  - Plan モードで設計
次のTODO: プラン策定
---

---
2026-03-07 15:11
作業項目: VB3-II ヴィンテージ・キャラクター CC 制御実装
追加機能の説明:
  - CC21=Key Click Level, CC22=Generator Leakage, CC23=Tone Aging を実装
  - setBfree tonegen.h/c にフィールド追加 (leakageGain, agingFactor, oscAgingGain)
  - midi.c に ccFuncNames + loadCCMap 追加
  - oscGenerateFragment() に Leakage ミキシング + Aging ゲイン適用を追加
  - iOS CCParameter.swift に Organ グループ追加
決定事項:
  - CC21-23 は setBfree デフォルトで #if 0 コメントアウト済み（未使用）なので安全に使用可能
  - Leakage は非アクティブ oscillator のフリーランニング波形を swlBuffer に微小信号として加算
  - Aging は高周波ほど強い減衰 (-12dB * aging * freq_ratio²)
次のTODO: ビルド検証 → Pi デプロイ → テスト
---

---
2026-03-07 15:34
作業項目: FxChain CC番号リマップ — setBfree CC と衝突回避
追加機能の説明:
  - Tremolo/Phaser (CC1-4) 以外の FxChain CC を 100 以降に移動
  - setBfree デフォルト CC (CC7,11,64,65,66,70-78,80-83,91-95) がそのまま通るようにする
  - SynthEngine の CC7/11/74 も移動対象
  - 旧CC → 新CC マッピング:
    - CC7 Volume → CC102, CC11 Expression → CC103
    - CC69 IR Select → CC104, CC74 IR Wet/Release → CC105
    - CC70 Drive → CC106, CC71 Tone → CC107
    - CC72 EQ Lo → CC108, CC73 EQ Hi → CC109
    - CC75 Space Mode → CC110, CC76 Space Time → CC111
    - CC77 Space Decay → CC112, CC78 Space Wet → CC113
    - CC79 Chorus Rate → CC114, CC80 Chorus Depth → CC115
    - CC81 Chorus Wet → CC116
  - iOS CCParameter.swift も同期更新
決定事項:
  - CC1-4 (Mod) と CC64 (Sustain) はそのまま維持
  - CC74 は SynthEngine で Release Time として使っていたが CC105 に移動
  - setBfree のデフォルトマッピングが全て有効になる
次のTODO: 実装 → ビルド検証
---

---
2026-03-07 15:39
作業項目: Pi デプロイ（CC リマップ + setBfree vintage character）
追加機能の説明:
  - FxChain CC を 100以降にリマップ（setBfree CC と衝突回避）
  - Release Time を CC5 (default=10) に変更
  - setBfree CC21-23 (Key Click / Leakage / Aging) 追加
決定事項:
  - rsync + cmake rebuild + restart
次のTODO: 演奏テスト
---

---
2026-03-07 15:46
作業項目: elepiano.service LV2 CC マッピング更新（旧CC→新CC）
追加機能の説明:
  - LV2 プラグインの CC マッピングも 100 以降に更新
  - 旧: CC68/70/71→Channel9, CC72/73→EQ, CC77/78→Chorus, CC79-84→TapeDelay, CC85→kPlateB
  - 新: CC106/107→Channel9 Drive/Tone, CC108/109→EQ, CC114/115→Chorus等
決定事項:
  - elepiano.service の Environment 行を更新
次のTODO: service 更新 → restart → テスト
---

---
2026-03-07 15:58
作業項目: iOS アプリ Swift コンパイルエラー修正 + デプロイ
追加機能の説明:
  - CCControlView.swift:100 ccToggle() 関数で return 文が欠落 → Swift が戻り型推論不可
  - let isOn = ... の後に HStack を返すため、return を明示追加
決定事項:
  - ccToggle 関数の HStack 前に return 追加
次のTODO: ビルド → デバイスデプロイ
---

---
2026-03-07 16:28
作業項目: BLE 接続問題の調査・修正
追加機能の説明:
  - BLE bridge がクラッシュループ → 原因: RF-kill で Bluetooth がブロック → rfkill unblock で解除
  - HCI advertising が bluetoothd 起動中は 0x0C (Command Disallowed) → Pi 再起動で解消
  - iOS アプリの BLE スキャンフィルタを緩和 (UUID限定 → 全デバイススキャン + UUID/名前フィルタ)
  - スキャンタイムアウトを 10秒 → 30秒 に延長
  - Advertisement から LocalName プロパティ削除、Includes=["local-name"] に変更（31byte制限対策）
  - StatusView に "Scanning..." / "Not connected" 表示追加（タップで再スキャン）
決定事項:
  - 接続成功の直接原因: Pi 再起動で HCI コントローラがクリーンな状態に復帰
  - RF-kill ブロックが根本原因（長時間 ble-bridge のクラッシュループで BT チップが不安定化）
  - BlueZ main.conf に Experimental=true, AutoEnable=true を追加
次のTODO: コミット
---

---
2026-03-07 16:37
作業項目: iOS Remote - Landscape フェーダー UI リニューアル
追加機能の説明:
  - SimpleMIDIController 風の縦フェーダー並びミキサーレイアウトに刷新
  - FaderView.swift: カスタム縦フェーダー (DragGesture, minimumDistance:0)
  - LandscapeCCView.swift: 左サイドバー(プログラム) + タブ(4ページ) + フェーダー群
  - CCParameter.swift: CCPage enum 追加 (Mix/Tone/Space/Organ)
  - StatusView.swift: コンパクトモード追加
  - ElepianoRemoteApp.swift: Landscape ロック + NavigationStack 除去
決定事項:
  - 4ページ構成: Mix(Vol/Exp/Mod/Rel), Tone(IR/OD/EQ), Space(Space/Chorus), Organ
  - 左サイドバー56pt, トップバー32pt, フェーダー高さ~280pt
  - グループ色: Volume=青, Mod=緑, IR/OD/EQ=オレンジ, Space/Chorus=紫, Organ=赤
次のTODO: 実装 → ビルド → デバイスデプロイ
---

---
2026-03-07 16:43
作業項目: iOS アプリ 実機デプロイ
追加機能の説明:
  - xcodebuild で実機デプロイ実行
決定事項:
  - ビルド成功済み、デプロイ試行中
次のTODO: デプロイ確認
---

---
2026-03-07 16:53
作業項目: iOS Landscape フェーダー UI v2 — 大型化 + ページ内容刷新
追加機能の説明:
  - フェーダー幅をフレキシブル化（画面横幅を均等分割）、trackWidth 40→56pt
  - Mix ページ刷新: Expression削除、Drive/Tone移動、8コントロール
  - Tone ページ: IR Mic セレクター（10値名前表示）、IR Wet/Lo EQ/Hi EQ
  - Space ページ: Mode セレクター（OFF/Tape/Room/Plate）、7コントロール
  - Organ ページ: 9本ドローバー（逆フェーダー）+ パーカッションスイッチ + キャラクター
  - SelectorView / DrawbarView 新規コンポーネント追加
決定事項:
  - セレクター: タップで次の値に進む
  - ドローバー: 上=0, 下=127 の逆フェーダー
  - Organ パーカッション: 4つのトグル (Perc/Vol/Decay/Harm)
次のTODO: CCParameter.swift → FaderView.swift → LandscapeCCView.swift の順で実装
---

---
2026-03-07 17:01
作業項目: iOS UI 修正 — CC送信不具合 + スワイプページ切替
追加機能の説明:
  - 左サイドバーのプログラムボタンがCC送信に干渉する問題を修正
  - ページタブをスワイプ（横スライド）で切り替えられるように TabView + .page スタイルに変更
決定事項:
  - TabView(selection:) + .tabViewStyle(.page) でスワイプ切替
  - タブバーのボタンと TabView の selection を同期
次のTODO: 実装 → ビルド → デプロイ
---

---
2026-03-07 17:04
作業項目: プログラムチェンジ動作確認 + デバッグログ追加
追加機能の説明:
  - iOS側のsendProgramChangeは0-indexedで送信、elepiano側も0-indexedで受信 — ロジック上は正しい
  - BLE接続状態やcharacteristic取得失敗の可能性を調査
  - printデバッグログを追加してBLE送信の確認を容易に
決定事項:
  - sendProgramChangeにprint追加、pcCharacteristic nil チェック
次のTODO: デバッグ → 原因特定
---

---
2026-03-07 17:07
作業項目: UI改善 — ボタンハイライト + サイドバー拡大 + Organ/Chorus/Vibrato
追加機能の説明:
  - プログラムボタン: BLEステータス待ちではなくローカルで即時ハイライト更新
  - サイドバーボタン: 画面高さいっぱいに拡大
  - Organ ページ: setBfree Vibrato/Chorus スイッチ (CC92/CC31) 追加
  - Space ページ: Chorus/Vibrato トグル追加
決定事項:
  - selectedProgram を @State で管理、sendPC後に即時反映
  - サイドバーボタンを Spacer() で均等配置
次のTODO: 実装 → ビルド → デプロイ
---

---
2026-03-07 17:12
作業項目: IR Wet デフォルト127 + IR名をセレクタ内に統合
追加機能の説明:
  - IR Wet のデフォルトを 0→127 に変更（常時ON）
  - IR パラメータは CC104(マイク選択) と CC105(wet) の2つのみ
  - 「モデル切替」= マイクポジション切替、10種の Twin Reverb キャビネットIR
決定事項:
  - IR Wet デフォルト 127
次のTODO: 実装 → デプロイ
---

---
2026-03-07 17:14
作業項目: LINE録音用プリアンプ/トランスIR候補調査
追加機能の説明:
  - Web検索でフリーのプリアンプ/トランスIR WAVを調査
  - キャビネットIRは豊富だが、プリアンプ単体のIRは希少
決定事項:
  - 候補を回答にまとめる
  - 無料プリアンプIRの直接DLが困難（CAPTCHA/Patreon有料/メール問合せ）
  - 代替: Pythonでプリアンプ風IRを生成（トランス色 = 偶数次高調波 + ローミッド強調 + HFロールオフ）
次のTODO: Python IR生成 → data/ir/ 配置 → Pi転送
---

---
2026-03-07 17:24
作業項目: LINE + CabIR 統合 — 全16種IR
追加機能の説明:
  - data/ir-all/ にプリアンプIR(6種) + Twin73 CabIR(10種) を統合
  - Pythonで生成した6種プリアンプIR: Neve Warm/Bright, API Punch, SSL Clean, Tape Warm, Direct
  - Pi に転送、ELEPIANO_IR_DIR を ir-all に変更、restart
  - iOS セレクタ名を16種に更新
  - Pi 正常起動確認: "[IR] loaded 16 IRs from /home/hakaru/elepiano/data/ir-all"
決定事項:
  - IR 01-06: LINE用プリアンプIR (512 samples)
  - IR 07-16: Twin73 CabIR (1000 samples)
  - CC104 セレクタで全16種を切替可能
次のTODO: 演奏テスト、IR の音質チューニング
---

---
2026-03-07 17:31
作業項目: CC7/CC11 ボリューム復活 + フェーダー操作修正
追加機能の説明:
  - CC7→CC102リマップでMIDIキーボードのCC7が効かなくなっていた
  - synth_engine.cpp: CC7をCC102のエイリアス、CC11をCC103のエイリアスとして追加
  - FaderView: .gesture → .highPriorityGesture に変更（TabViewスワイプとの衝突解消）
決定事項:
  - CC7/CC102 両方でボリューム制御可能（setBfreeのCC7とは両方に効く）
  - CC11/CC103 両方でエクスプレッション制御可能
次のTODO: 演奏テスト
---

---
2026-03-07 17:39
作業項目: iOS アプリ TabView 削除修正 → ビルド・デプロイ
追加機能の説明:
  - TabView .page スタイルのスワイプがフェーダー DragGesture を消費する問題の修正
  - TabView を完全に除去、pageContent(selectedPage) で直接レンダリング
  - FaderView は .highPriorityGesture を維持
決定事項:
  - フェーダー操作が効かない根本原因は TabView ジェスチャー衝突
  - 修正済みコードを両デバイスにデプロイする
次のTODO: ビルド → 実機デプロイ
---

---
2026-03-07 18:01
作業項目: BLE→MIDI 再接続問題の修正 + IR レベル比較
追加機能の説明:
  - BLE bridge が elepiano 再起動後に CC を送れなくなる問題を修正
  - 原因: AlsaMidiSender が起動時の elepiano client ID を保持し、elepiano 再起動で client ID が変わると不一致
  - 修正: reconnect() メソッド追加、10秒ごとに client ID を確認して再接続
  - SUBSCRIBERS アドレッシングは ALSA seq で動作しなかったため、直接アドレッシングを維持
  - IR レベル比較: LINE IR (peak -0.4dB) vs Twin73 IR (peak -3〜-4dB)
決定事項:
  - LINE IR の peak が Twin73 より約3dB 高い（LINE IR が大きい方）
  - Twin73 は実測キャビネット IR なのでレベルが自然に低い
次のTODO: IR レベル正規化の検討
---

---
2026-03-07 18:12
作業項目: BLE 自動再接続の完全自動化
追加機能の説明:
  - Pi側: ble-bridge.service に PartOf=elepiano.service + ExecStartPre=sleep 5 追加
    → elepiano 再起動時に BLE bridge も自動連動再起動
  - Pi側: AlsaMidiSender の _find_elepiano_client を ctypes 直接呼び出しに変更
    → aconnect コマンドのタイムアウト問題を解消
  - Pi側: 10秒ごとの reconnect() で elepiano client ID 変更を検知・再接続
  - iOS側: stopScan() 後に未接続なら5秒後にリトライスキャン
    → BLE bridge 再起動後も自動再接続
  - LINE IR レベルを Twin73 と同等に正規化（peak 0.95 → 0.625）
決定事項:
  - systemd PartOf で elepiano/BLE bridge のライフサイクルを連動
  - iOS は未接続時に無限リトライスキャン
次のTODO: 動作確認
---

---
2026-03-07 18:18
作業項目: elepiano プロジェクト性能監査（src/ble/ios 全体）
追加機能の説明:
  - リアルタイムオーディオスレッドの禁止操作検出
  - 不要コピー・計算、キャッシュ効率、ALSA/MIDIレイテンシ
  - BLE通信スロットリング、iOS SwiftUI再描画
  - IR畳み込み計算効率、SpscQueueサイズ適正性
決定事項:
  - 全ソースファイルを読み込んで分析中
次のTODO: 分析結果を Impact 分類で報告
---

---
2026-03-07 18:18
作業項目: 全変更ファイルのコードレビュー
追加機能の説明:
  - git diff で確認できる全変更ファイル（staged + unstaged + untracked）を対象にレビュー
  - 対象: alsa_midi_sender.py, ble_bridge.py, gatt_service.py, synth_engine.cpp,
    BLEManager.swift, ElepianoRemoteApp.swift, CCParameter.swift, CCControlView.swift,
    StatusView.swift, FaderView.swift, LandscapeCCView.swift, Info.plist
  - 観点: ロジック正しさ、API使用法、エラーハンドリング、スレッド安全性、コード一貫性
決定事項:
  - レビュー結果を severity 分類で報告
次のTODO: 指摘事項の修正
---

---
2026-03-07 18:18
作業項目: ドキュメント作成 (CC_MAP.md / ARCHITECTURE.md / DEPLOY.md)
追加機能の説明:
  - ソースコードを読んで正確な情報を収集
  - docs/CC_MAP.md: 全CC番号マッピング表（SynthEngine/FxChain/LV2/setBfree）
  - docs/ARCHITECTURE.md: コンポーネント図、BLE Bridge 構成、シグナルフロー
  - docs/DEPLOY.md: Pi/iOS ビルド手順、systemd 設定、環境変数一覧
決定事項:
  - ソースから直接情報収集し、正確なドキュメントを作成
次のTODO: ドキュメント完成後に git commit
---

---
2026-03-07 18:18
作業項目: セキュリティ監査 (ble/src/ios)
追加機能の説明:
  - BLE GATT 認証なし CC/PC/コマンド受付リスク
  - on_command() systemctl コマンドインジェクション
  - MIDI/BLE データバリデーション、バッファオーバーフロー
  - 整数オーバーフロー CC/velocity 範囲チェック
  - root 権限 Python 実行リスク
  - ctypes unsafe 操作
  - ALSA sequencer 入力バリデーション
  - iOS 通信セキュリティ
決定事項:
  - 全ソースファイル読み込み完了、脆弱性分析実施
次のTODO: 脆弱性レポート作成、修正提案
---

---
2026-03-07 18:18
作業項目: リファクタリング分析・提案（コード変更なし）
追加機能の説明:
  - src/ (C++ シンセエンジン 27ファイル), ble/ (Python BLE bridge 4ファイル), ios/ (SwiftUI 10ファイル) を全読み込み
  - 7観点で分析: 重複、責務分離、モジュール構造、抽象化、命名一貫性、マジックナンバー、ハードコード
  - Priority (High/Medium/Low) で分類し、具体的手順を提案
決定事項:
  - コード変更は行わず、分析と提案のみ
次のTODO: ユーザーの承認後に個別リファクタリングを実施
---

---
2026-03-07 18:19
作業項目: elepiano 性能監査レポート（8観点、全ファイル精査完了）
追加機能の説明:
  - 全ソースファイル（C++ 27, Python 4, Swift 10）を精読して性能問題を分析
  - 8観点: RTスレッド禁止操作、不要コピー/計算、キャッシュ効率、ALSA/MIDIレイテンシ、BLEスロットリング、iOS再描画、IR畳み込み効率、SpscQueueサイズ
決定事項:
  - High (5件): IR O(N*M) 直接畳み込み、WAV dump fwrite RTスレッド内、fprintf RTスレッド内(複数箇所)、Lv2Host::set_cc 線形探索、FxChain process_phaser 毎サンプル std::exp
  - Medium (6件): FxChain巨大スタック構造体、OrganEngine 128ノート全走査、StatusReporter RTスレッドから呼び出し可能、BLE StatusMonitor 切断検知5秒遅延、alsa_midi_sender restype設定位置、LV2 in/out バッファ8192固定確保
  - Low (5件): SpscQueue FxEvent 32エントリ小さめ、Voice::mix 分岐予測、iOS CC Dictionary lookup、chorus/delay バッファゼロ初期化、SampleDB::find candidates_ 未使用メンバ
  - コード変更なし（監査レポートのみ）
次のTODO: ユーザーの優先度判断を受けて改善実装
---

---
2026-03-07 18:22
作業項目: 5エージェント監査結果の統合サマリー提示
追加機能の説明:
  - コードレビュー、性能監査、セキュリティ監査、リファクタリング提案、ドキュメント作成の全5件完了
  - 各エージェントの結果を統合し、優先度別にサマリー提示
決定事項:
  - 全5エージェント完了確認済み
  - Critical 修正（ctypes restype/argtypes）は即時適用済み
次のTODO: ユーザーに統合結果を報告、次のアクション決定
---

---
2026-03-07 18:22
作業項目: 監査結果を GitHub Issue に登録
追加機能の説明:
  - 新規 issue 10件作成:
    - #45 RT fprintf (bug)
    - #46 phaser exp() per sample (enhancement)
    - #47 set_cc linear search (enhancement)
    - #48 BLE GATT auth (bug)
    - #49 OrganEngine 128 note scan (enhancement)
    - #50 FxChain 752KB buffer cache (enhancement)
    - #51 IR ring buffer modulo (enhancement)
    - #52 FxChain CC enum化 (enhancement)
    - #53 SetBfreeEngine unique_ptr (enhancement)
    - #54 StatusMonitor timeout (enhancement)
  - #38 (BLE MIDI reconnect) をクローズ（実装済み）
決定事項:
  - 既存 issue (#26,40,41) と重複しない項目のみ新規作成
  - #38 は reconnect() + systemd PartOf で解決済み
次のTODO: ユーザーの優先度判断を受けて改善実装
---

---
2026-03-07 22:52
作業項目: オープン Issue 整理 — 4 Tier 優先度プラン策定
追加機能の説明:
  - 15件のオープン issue を4段階に分類
  - Tier 1: RT安全性 (#26,#45) — underrun 直結、最優先
  - Tier 2: 性能改善 (#46,#47,#51,#41,#49,#50) — CPU削減
  - Tier 3: セキュリティ (#48,#40) — BLE認証
  - Tier 4: コード品質 (#52,#53,#54,#36,#30) — 急ぎではない
決定事項:
  - Phase 1→2→3→4 の順で実装
  - 旧プラン（iOS UI v2）は実装済みのため上書き
次のTODO: ユーザー承認後に Phase 1 から実装開始
---

---
2026-03-07 22:53
作業項目: Issue整理プラン実装開始 — Phase 1 (RT安全性 #26+#45) + Phase 2 (#46+#47+#51)
追加機能の説明:
  - Phase 1: RTスレッド内の fprintf/fwrite を SpscQueue 経由の非同期ログに置換
  - Phase 2: phaser exp() 定数化、set_cc 固定配列化、IR ring buffer ビットマスク化
  - Phase 3: BLE GATT encrypt-write
  - Phase 4: コード品質改善
決定事項:
  - 全4フェーズを順次実装
次のTODO: Phase 1 実装中
---

---
2026-03-07 23:11
作業項目: Phase 1-4 全実装完了 — RT安全性 + 性能改善 + BLEセキュリティ + コード品質
追加機能の説明:
  Phase 1 (RT安全性 #26+#45):
  - src/rt_log.hpp 新規: SpscQueue<RtLogEntry,64> による非同期ログ
  - audio_output.cpp: WAV dump を lock-free ring buffer 経由に変更（fwrite排除）
  - audio_output.cpp: snd_pcm_recover fprintf → rt_log
  - synth_engine.cpp: _program_change fprintf → rt_log
  - fx_chain.cpp: CC3/CC110 fprintf → rt_log
  - ir_convolver.cpp: select() fprintf → rt_log
  - main.cpp: run_engine ループに drain_rt_log() + flush_wav_dump() 追加
  Phase 2 (性能改善 #46+#47+#51):
  - fx_chain.cpp: process_phaser の exp() をブロック単位に削減 (2*frames → 2回/ブロック)
  - fx_chain.hpp: phaser_log_min_/phaser_log_range_ を定数メンバ化
  - lv2_host.cpp: set_cc を CcMapping cc_lut_[128] 直接参照に変更（hash+線形探索→O(1)）
  - ir_convolver: ring_size_ を2の冪に切り上げ、ビットマスクで modulo/conditional 排除
  Phase 3 (BLEセキュリティ #40+#48):
  - gatt_service.py: CC/PC/BatchCC/Command の Flags に encrypt-write 追加
  - ObjectManager の GetManagedObjects プロパティも同期更新
  - ペアリング済みデバイスのみ書き込み可能に
  Phase 4 (コード品質 #53):
  - setbfree_engine: raw pointer → std::unique_ptr<Impl> に変更
決定事項:
  - Pi オフラインのためビルド検証は次回接続時に実施
  - 全変更は構文チェック済み、ブレース均衡OK
次のTODO: Pi 接続時にビルド検証 → デプロイ → 演奏テスト
---
