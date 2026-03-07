# Claude Worklog 2026-03-06

---
2026-03-06 10:31
作業項目: git pull 実行
追加機能の説明: なし
決定事項: なし
次のTODO: pull結果を確認
---

---
2026-03-06 10:32
作業項目: ClaudeWorklog20260304.md のローカル変更を破棄して git pull
追加機能の説明: なし
決定事項: ローカル変更を捨てて origin/main をマージ
次のTODO: なし
---

---
2026-03-06 10:34
作業項目: Pi5 の現状確認
追加機能の説明: なし
決定事項: tasks/todo.md・最新ワークログ・src/を確認
次のTODO: 確認結果をユーザーに報告
---

---
2026-03-06 12:51
作業項目: Tailscaleトラブルドキュメント案内
追加機能の説明: なし
決定事項: docs/20260303_223002_tailscale-macos-local-network-interference.md を確認
次のTODO: なし
---

---
2026-03-06 10:36
作業項目: Pi5 で git pull + 再ビルド
追加機能の説明: FXチェーン統合、flac_decoder/audio_output/main 更新
決定事項: Pi5 側に最新コードを反映する
次のTODO: ビルド成功・動作確認
---

---
2026-03-06 10:37
作業項目: Pi5 SSH接続 → git pull + 再ビルド実行
追加機能の説明: なし
決定事項: hakarupiano.local に SSH して実行
次のTODO: ビルド結果確認
---

---
2026-03-06 13:26
作業項目: elepiano 起動・MIDI接続確認
追加機能の説明: なし
決定事項: ローカルMacでビルド＆起動してMIDI接続状態を確認
次のTODO: 起動結果報告
---

---
2026-03-06 13:29
作業項目: SSH鍵設定 + elepiano起動・MIDI接続確認
追加機能の説明: なし
決定事項:
  - SSH ED25519鍵生成・Pi登録完了
  - Keystage MIDIキーボード認識済み (client 24)
  - elepiano起動成功 (attack 1384サンプル, hw:2,0 Keystage USBオーディオ)
  - PiSoundは未接続、release FLACはPi上に未転送
次のTODO: 弾いて動作確認、release サンプル転送
---

---
2026-03-06 13:50
作業項目: デジタルノイズ原因特定
追加機能の説明: なし
決定事項: FxChainではなくサンプルデータ自体が壊れているのが原因
次のTODO: サンプル再抽出 or 破損箇所の特定・修復
---

---
2026-03-06 13:55
作業項目: ペダル対応 + レイテンシ改善
追加機能の説明:
  - SynthEngineにCC64サステインペダル実装（ペダル中はnote_offを遅延）
  - レイテンシ削減（period_size/periods調整）
決定事項:
  - rhodes-classic-cleanでノイズ解消確認済み
  - ペダル未実装・レイテンシが課題
次のTODO: 実装・ビルド・テスト
---

---
2026-03-06 14:00
作業項目: リアルタイムMIDIシンセのレイテンシ根本改善調査
追加機能の説明: なし（調査タスク）
決定事項:
  - 現状: ALSA hw:2,0 Keystage USB Audio, period=128, periods=3, buffer=384 @ 44100Hz
  - S16LE フォーマット、SCHED_FIFO priority=80
  - NEON SIMD で float→S16変換最適化済み
  - snd_pcm_writei ブロッキングループ、underrun検出済み
  - 調査対象: ALSA最適化, USB Audio制約, I2S DAC HAT, RTカーネル, JACK/PipeWire
次のTODO: 調査結果をまとめて報告
---

---
2026-03-06 14:00
作業項目: ドキュメント監査と改善提案
追加機能の説明: コード変更なし、分析・提案のみ
決定事項: docs/、README.md、src/、tools/ の現状を確認して改善提案をまとめる
次のTODO: 監査結果を報告
---

---
2026-03-06 14:00
作業項目: C++シンセエンジン リファクタリング分析
追加機能の説明: コード変更なし、全src/ファイル(9cpp+9hpp)を読んで改善提案を策定
決定事項:
  - 7つの観点(引数パース/Engine共通化/Config/FxChainパラメータ/SampleDB責務/ヘッダ依存/ファイル分割)で分析
  - 過剰設計を避けた実用的提案に限定
次のTODO: 分析結果を優先度付きで報告
---

---
2026-03-06 14:03
作業項目: 6エージェント並列監査 総合結果
追加機能の説明: なし（分析のみ）
決定事項:
  全監査共通Critical: FxChain set_param/process のデータ競合（SPSCキュー化で修正必要）
  性能: FLAC二重I/O、DELAY_BUF非2冪、tanhf→多項式近似、note_index→固定配列
  コードレビュー: drflacリーク(RAII化)、Voice::mix返り値未使用、MidiEventフィールド不整合
  セキュリティ: MIDI/JSON入力検証は良好、dr_flac更新推奨、setcap推奨(sudo回避)
  リファクタリング: FxChain定数化(中)、SampleDBコンストラクタ分割(中)、Engine基底は見送り
  ドキュメント: README.md不在(高)、CCマッピング分散(中)
  レイテンシ: I2S DAC HAT導入が最大効果(2-4ms達成可能)、period=64/periods=2で即改善
次のTODO: FxChainデータ競合修正 → FLAC二重I/O解消 → README作成
---

---
2026-03-06 15:11
作業項目: 監査結果に基づく優先修正6件の一括実装
追加機能の説明:
  1. [Critical] FxChain データ競合修正 - SpscQueue<FxEvent,32>で set_param/process間の同期
  2. [High] FLAC二重ファイルI/O解消 - drflac_open_memoryでメモリから1回読み
  3. [High] drflac RAIIラッパー - unique_ptr+カスタムデリータでリーク防止
  4. [Medium] DELAY_BUF 2のべき乗化 - 22050→32768、%→&マスク
  5. [Medium] MIDIキューサイズ拡大 - 64→256
  6. [Medium] CLI引数バリデーション - period_size/periods範囲チェック
決定事項: 6件全て実装する
次のTODO: 実装→ビルド確認
---

---
2026-03-06 15:59
作業項目: Pi5テスト・PiSound導入・壊れたサンプル削除
追加機能の説明:
  - MIDI自動接続 (subscribe_all_hw) 追加 — aconnect不要に
  - FLACログにファイル名表示追加
  - PiSound HAT セットアップ (dtoverlay=pisound-pi5, dtparam=spi=on, Blokas APT)
  - CRC-16 NG の壊れたFLAC 37ファイル削除、samples.json更新 (1022→985)
決定事項:
  - PiSound認識成功 (hw:2,0, FW 1.03, HW 1.2, Serial PS-3WPC1X3)
  - PiSoundネイティブレート 48000Hz で動作
  - 音出し成功確認
次のTODO: 音質・レイテンシ評価、period_size最適化
---

---
2026-03-06 16:06
作業項目: レイテンシさらなる改善 — 現状分析と対策検討
追加機能の説明: なし（調査）
決定事項:
  - 現行: PREEMPT カーネル 6.12.47, CPUガバナー=ondemand(1.6GHz)
  - RT カーネル linux-image-rt-arm64 6.12.73-1 が APT で利用可能
  - 改善策を優先度順に提案
次のTODO: ユーザー判断で RT カーネル導入 or チューニング実施
---

---
2026-03-06 16:13
作業項目: レイテンシ改善完了・次のステップ提案
追加機能の説明: L1(CPUガバナー), L2(threadirqs), L5(cyclictest), L6(period=32) 完了
決定事項:
  - period=32/periods=2 @ 48kHz = 1.3ms で安定動作、underrun ゼロ
  - cyclictest Max=5μs — RT カーネルは当面不要
次のTODO: 次の改善項目をユーザーと選定
---

---
2026-03-06 16:30
作業項目: ピッチ修正 + リリースサンプル動作確認
追加機能の説明:
  - main.cpp: audio.sample_rate() (ALSA実際値48000) をSynthEngine/FxChainに渡すよう修正
  - voice.cpp: pitch_ratio に sd->sample_rate/output_rate 比を適用
  - 根本原因: AudioOutput::open_pcm()がcfg_のコピーを更新、呼び出し元acfgは未更新だった
決定事項:
  - ピッチ修正確認OK（44100Hzサンプル → 48kHz PiSound出力で正しいピッチ）
  - attack(985) + release(1759) サンプル同時動作確認OK
  - period=32/periods=2 @ 48kHz = 1.3ms レイテンシで安定
次のTODO: systemd自動起動、BLE-MIDIアプリ、他音色
---

---
2026-03-06 16:33
作業項目: PREEMPT_RT カーネル導入計画 — ExitPlanMode
追加機能の説明: なし（計画のみ）
決定事項:
  - linux-image-rpi-v8-rt (6.12.62) を使用
  - config.txt に kernel=kernel8-rt.img 追加
  - ロールバックは kernel= 行削除のみ
次のTODO: ユーザー承認後に実行
---

---
2026-03-06 16:34
作業項目: PREEMPT_RT カーネル導入実行
追加機能の説明:
  - linux-image-rpi-v8-rt パッケージインストール
  - /boot/firmware/config.txt に kernel=kernel8-rt.img 追加
  - 再起動後 PiSound認識・cyclictest・elepiano動作確認
決定事項: 計画通り実行開始
次のTODO: Step1 apt install → Step2 config.txt編集 → Step3 reboot → 確認
---

---
2026-03-06 16:49
作業項目: PREEMPT_RT カーネル導入完了
追加機能の説明:
  - linux-image-rpi-v8-rt 6.12.62 インストール成功
  - config.txt に kernel=kernel8_rt.img 追加（初回 kernel8-rt.img でタイポ→修正）
  - PREEMPT_RT カーネル起動確認
決定事項:
  - uname: 6.12.62+rpt-rpi-v8-rt / PREEMPT_RT 確認OK
  - PiSound hw:2,0 認識OK（FW 1.03, HW 1.2）
  - cyclictest 500K回: Max=6μs, Avg=1μs（PREEMPT時 Max=5μs とほぼ同等）
  - elepiano 起動OK（period=32/periods=2 @ 48kHz, attack+release サンプル）
  - ロールバック: config.txt の kernel= 行削除→reboot で元のrpi-2712カーネルに戻せる
次のTODO: 演奏テスト（underrunなし・ピッチ正常を確認）
---

---
2026-03-06 16:50
作業項目: USB MIDI vs PiSound MIDI のレイテンシ比較についての質問回答
追加機能の説明: なし（情報提供）
決定事項: USB MIDIとPiSound MIDIの仕組みの違いを説明
次のTODO: なし
---

---
2026-03-06 16:52
作業項目: USB MIDI接続に切り替え
追加機能の説明: なし
決定事項:
  - PiSound MIDI (24:0→128:0) を切断
  - Keystage USB MIDI (28:0→128:0) に接続
  - Keystage ポート0 (CTRL) を使用
次のTODO: 弾いて比較テスト
---

---
2026-03-06 16:53
作業項目: サンプル先頭無音区間の調査・体感レイテンシ原因特定
追加機能の説明: なし（調査タスク）
決定事項:
  - 全attackサンプルの先頭に4097サンプル(92.9ms)の完全無音区間が存在
  - FLACブロックサイズ4096+1サンプルがゼロパディング — 元SpCAデータ由来
  - voice.cpp は position=0.0 から再生開始（オフセットなし）
  - synth_engine.cpp の MIDI→Voice割り当ては即座（遅延なし）
  - ALSAバッファ1.33msは問題なし — 92.9msの無音が体感レイテンシの主因
  - releaseサンプルは0〜29ms程度（ばらつきあり）
改善案:
  - sample_db.cpp のロード時に先頭無音をトリミング（最も確実）
  - または voice.cpp の note_on で start_offset を設定
次のTODO: 先頭無音トリミング実装
---

---
2026-03-06 16:53
作業項目: 先頭無音トリミング実装・ビルド・起動
追加機能の説明:
  - sample_db.cpp: PCMロード後に先頭の連続ゼロサンプル(|v|<1e-6f)を除去
  - 92.9ms の無音区間を除去 → 理論レイテンシ 1.33ms のみに
決定事項:
  - ビルド成功、elepiano起動OK
  - USB MIDI + PiSound MIDI 両方自動接続
次のTODO: 弾いてレイテンシ改善を体感確認
---

---
2026-03-06 17:00
作業項目: 音の変化の原因調査
追加機能の説明: なし（調査）
決定事項:
  - FLACファイル自体は無変更（先頭4097サンプル無音はそのまま）
  - トリミングはsample_db.cppのロード時のみ
  - 最初の非ゼロサンプル[4097]が即座に0.01レベル — 音の立ち上がりは切れていない
  - releaseサンプルは先頭無音なし（onset=0）— トリミングの影響なし
  - 音の変化は93ms遅延がなくなったことによる知覚変化の可能性が高い
次のTODO: ユーザーが弾いて確認
---

---
2026-03-06 17:03
作業項目: 次のTODO確認
追加機能の説明: なし
決定事項: todo.md レビュー、次の作業候補を提示
次のTODO: ユーザー選択待ち
---

---
2026-03-06 17:05
作業項目: systemd 自動起動サービス作成
追加機能の説明:
  - elepiano.service ユニットファイル作成
  - Pi電源ON → 自動で elepiano 起動 → MIDI接続 → 即演奏可能
決定事項:
  - /etc/systemd/system/elepiano.service 作成・enable済み
  - active (running) 確認OK (PID 2634)
  - SCHED_FIFO prio=80, Nice=-20, RLIMIT_RTPRIO=99, MEMLOCK=infinity
  - After=sound.target, Restart=on-failure
  - PiSound MIDI 自動接続OK (subscribe_all_hw)
次のTODO: reboot して自動起動を確認、弾いてテスト
---

---
2026-03-06 17:07
作業項目: Pi5 起動時間短縮
追加機能の説明:
  - 不要サービス無効化で起動時間を13.6s→大幅短縮
  - sound.target @3s なので elepiano を3-4秒で起動可能に
決定事項:
  - 現状: kernel 1.4s + userspace 12.2s = 13.6s
  - ボトルネック: NM-wait-online(6s), NM(2.8s), cloud-init(1.4s)
  - 無効化対象: NM-wait-online, cloud-init, ModemManager, cups-browsed, colord
次のTODO: 不要サービス無効化 → reboot → 確認
---

---
2026-03-06 17:07
作業項目: Pi5 起動時間短縮 + systemd自動起動 完了
追加機能の説明:
  - 無効化: NM-wait-online, cloud-init(3件), ModemManager, cups(4件), colord
  - systemd-rfkill.service/socket を mask（ハング防止）
決定事項:
  - 起動時間: 13.6s → 10.7s（kernel 1.6s + userspace 9.1s）
  - elepiano 起動開始: @2.3s（sound.target直後）
  - サンプルロード完了: 起動後 ~52秒（985 attack + 1759 release FLAC）
  - PiSound MIDI 自動接続OK
  - 電源ON → 約55秒で演奏可能（ボトルネックはFLACロード）
次のTODO: FLACロード高速化（並列化 or キャッシュ）を検討
---

---
2026-03-06 17:11
作業項目: サンプルロード並列化実装
追加機能の説明:
  - SampleDB コンストラクタで std::thread を使い4コア並列FLACデコード
  - JSON解析→タスクリスト作成→並列デコード→結果マージ
  - 52秒→~13秒を期待
決定事項:
  - 並列FLACデコード実装完了（std::thread + atomic カウンタ）
  - PCMキャッシュ実装完了（.pcm_cache バイナリファイル、samples.jsonより新しければ使用）
  - コールドブート: 52秒→17秒（3倍高速化）
  - ウォームキャッシュ: 1秒（956ms + 31ms）
  - キャッシュサイズ: attack 1.4GB + release 52MB
  - 電源ON→演奏可能: 55秒→約18秒
次のTODO: コミット
---

---
2026-03-06 17:21
作業項目: FxChain CCコントロール確認
追加機能の説明: なし（確認のみ）
決定事項:
  - FxChain CCコントロールは既に実装済み
  - main.cpp:191-192 で全CC → fx.set_param() に転送
  - fx_chain.cpp:42-63 の apply_param() で CC70-79 を処理
  - SpscQueue経由のロックフリー設計（MIDIスレッド→オーディオスレッド）
次のTODO: Keystage側のCCマッピング確認 or 追加CC対応
---

---
2026-03-06 17:27
作業項目: FxChain改善 — CC1トレモロ + テープエコー化
追加機能の説明:
  - CC1 (モジュレーションホイール) → Tremolo Depth に割り当て
  - Delay をテープエコー風に改良（ドットエイス対応、ウォームなLPF、Wet/Dry分離）
決定事項:
  - CC1 → Tremolo Depth (0〜0.8) 実装完了
  - テープエコー改良:
    - LPF カットオフ 0.3→0.15（≈1.5kHz、テープヘッドの高域ロス）
    - tanhf サチュレーション追加（繰り返すごとに歪みが増す）
    - Wet/Dry 分離（CC80 で Wet レベル調整可能）
    - デフォルト: 375ms (ドット8分@120BPM), feedback=0.4, wet=0.5
  - CC75: ディレイタイム 50〜500ms
  - CC76: フィードバック 0〜0.85
  - CC80: ディレイ Wet レベル
  - ビルド・起動OK
次のTODO: 弾いてテスト
---

---
2026-03-06 17:35
作業項目: プリアンプをオーバードライブ風に改良
追加機能の説明:
  - tanhf ソフトクリップ → 非対称クリッピング + 偶数次倍音 + トーンフィルタ
  - Rhodes オーバードライブ（Suitcase amp風）の品のない歪み
決定事項:
  - 非対称クリッピング実装: 正側=3次多項式ハードクリップ、負側=tanhfソフト
  - 偶数次倍音が出る（真空管アンプ/Suitcase amp風）
  - ポストドライブ トーンフィルタ（1次LPF）追加
  - CC81: OD Tone (0=dark, 1=bright) 追加
  - drive上げると自然に暗くなる（アンプの特性を再現）
  - ビルド・起動OK
次のTODO: 弾いてテスト
---

---
2026-03-06 17:38
作業項目: CC番号を機能グループごとに整列
追加機能の説明: CC マッピングを機能別に連番化
決定事項:
  - Tremolo: CC1(Depth), CC2(Rate)
  - Overdrive: CC70(Drive), CC71(Tone)
  - EQ: CC72(Lo), CC73(Hi)
  - Tape Echo: CC75(Time), CC76(Feedback), CC77(Wet)
  - Chorus: CC78(Rate), CC79(Depth), CC80(Wet)
  - ビルド・起動OK
次のTODO: 弾いてテスト
---

---
2026-03-06 17:41
作業項目: Program Change 音色切替 (PG1=Rhodes, PG2=Wurlitzer, PG3=Vintage Vibe)
追加機能の説明:
  - MIDI Program Change で3音色を切替可能にする
  - Wurlitzer/Vintage Vibe のFLACをPiに転送
  - SynthEngine に複数 SampleDB 対応を追加
決定事項: 実装開始
次のTODO: FLAC転送→コード実装→ビルド→テスト
---

---
2026-03-06 17:43
作業項目: 外部ディスク Media のFLACファイル調査
追加機能の説明: なし（調査）
決定事項:
  - /Volumes/Media マウント済み、Keyscape .db ファイル51個確認
  - ローカル data/ には11音色分の抽出済みFLACあり
  - Keyscape Library に未抽出の音色多数（Clavinet, Pianet, CP-70B, Toy Piano等）
次のTODO: 必要な音色を選んで extract_samples.py で抽出
---

---
2026-03-06 17:50
作業項目: Rhodes LA Custom + LA Custom C7 Grand サンプル抽出
追加機能の説明:
  - extract_samples.py に4モード追加: rhodes-la-attack, rhodes-la-rel, c7grand-attack, c7grand-rel
  - 新規パーサー: parse_lacrm_name, parse_lacr_rel_name, parse_lacppu_name, parse_lacp_rel_name
決定事項:
  - Rhodes LA Custom: attack 2819 FLAC (2.0GB, mono), release 1408 FLAC (47MB)
  - LA Custom C7 Grand: attack 1970 FLAC (5.4GB, stereo!), release 1760 FLAC (661MB)
  - 全て非暗号化（SpCA→fLaC置換のみ）
  - 44100Hz/24bit、C7 Grand はステレオ
次のTODO: Piへ転送、Program Change で音色切替に組み込み
---

---
2026-03-06 18:00
作業項目: コードレビュー + コミット + プッシュ
追加機能の説明: 本日の全変更をまとめてコミット
決定事項: レビュー後コミット
次のTODO: コミット実行
---

---
2026-03-06 18:02
作業項目: リリースタイム CC 調整可能化
追加機能の説明:
  - CC74 でリリースタイム調整 (50ms〜2000ms)
  - SynthEngine経由で全ボイスのrelease_rateを更新
決定事項: 実装開始
次のTODO: 実装→ビルド→テスト
---

---
2026-03-06 18:04
作業項目: CC74 リリースタイム調整 — synth_engine 実装
追加機能の説明:
  - SynthEngine に release_time_s_ メンバ追加 (デフォルト 0.200s)
  - CC74 ハンドラ: 50ms〜2000ms 範囲で調整
  - _note_on / _start_release_voice で release_time_s_ を Voice に渡す
決定事項: voice.hpp/cpp は前回修正済み、synth_engine のみ変更
次のTODO: ビルド確認
---

---
2026-03-06 18:09
作業項目: CC74 リリースタイム範囲変更
追加機能の説明:
  - CC74=0 をデフォルト (200ms) に変更
  - CC74=127 で最長 200ms（変更なし＝CC74 は短縮方向のみ）
  - CC74=0: 0ms (即切れ), CC74=127: 200ms, デフォルト 50ms
  - voice.cpp: release_time_s≤1ms で即リリース (inf防止)
決定事項: CC74=0で0ms, デフォルト50ms, CC74=127で200ms
次のTODO: ビルド・デプロイ
---

---
2026-03-06 18:14
作業項目: Wurlitzer 音切れ修正 — release有無でデフォルト切替
追加機能の説明:
  - Program Change 時に release_db_ の有無で release_time_s_ を自動設定
  - release サンプルあり (Rhodes): 50ms
  - release サンプルなし (Wurlitzer/Vintage Vibe): 200ms
  - CC74 で引き続き手動調整可能
決定事項: 音色ごとにリリースフェード時間を自動切替
次のTODO: ビルド・デプロイ・テスト
---

---
2026-03-06 18:17
作業項目: Wurlitzer sustain欠落修正 — MAX_SAMPLE_FRAMES 拡大
追加機能の説明:
  - MAX_SAMPLE_FRAMES 441000 (10s) → 882000 (20s) に拡大
  - Wurlitzer サンプルは ~463000 フレーム (10.5s) で 441000 制限でカットされていた
  - キャッシュバージョン 2→3 に更新（再生成を強制）
決定事項: サンプル長制限が原因、sustainは元データにある
次のTODO: ビルド・デプロイ（キャッシュ再生成のため初回ロードに時間がかかる）
---

---
2026-03-06 18:22
作業項目: OOM修正 — MAX_SAMPLE_FRAMES 882000→529200 (12秒)
追加機能の説明:
  - 20秒は3音色同時ロードでOOM（swap 2GB使い切り→スラッシング）
  - 12秒に縮小（Wurlitzer 10.5秒をカバーしつつメモリ節約）
  - キャッシュバージョン 3→4
決定事項: vmstatでswpd=2097140, free=33MB, wa=75%を確認しOOMと断定
次のTODO: ロード完了待ち→テスト
---

---
2026-03-06 18:25
作業項目: Vintage Vibe 削除 + MAX_SAMPLE_FRAMES 元に戻す
追加機能の説明:
  - 3音色同時ロードでOOM（12秒設定でも発生）
  - Vintage Vibe を一旦削除、PG1=Rhodes PG2=Wurlitzer の2音色構成に
  - MAX_SAMPLE_FRAMES=441000 (10秒) に戻す
  - systemd サービスも2音色に変更
決定事項: メモリ制約のため2音色構成に
次のTODO: ビルド・デプロイ・systemd更新
---

---
2026-03-06 18:32
作業項目: PiSound音質評価 + I2S DAC HAT比較調査
追加機能の説明: なし（調査）
決定事項:
  - PiSound: PCM5102A DAC, SNR 110dB, THD<0.05%, 48/96/192kHz対応
  - 上位候補: HiFiBerry DAC2 HD (PCM1796, SNR>112dB, THD<0.003%)
  - 最上位候補: ES9038Q2M系 (DNR 129dB, THD+N -120dB, 384kHz/32bit, DSD512)
次のTODO: ユーザー判断
---

---
2026-03-06 18:37
作業項目: Durio Sound DAC 調査
追加機能の説明: なし（調査）
決定事項:
  - Durio Sound Pro: PCM5102 DAC（PiSoundと同じチップ）, SNR 112dB, 192kHz/24bit
  - DualMono構成可（Basic+Pro, SNR +3dB）
  - PCB設計・電源回路に注力した高品質設計だが、DACチップ自体はPiSoundと同クラス
  - ES9038Q2M系と比較するとスペック上は劣る
次のTODO: なし
---

---
2026-03-06 18:29
作業項目: Wurlitzer 200A FLAC 大量スキップ問題の根本原因調査
追加機能の説明: なし（調査のみ、コード変更なし）
決定事項:
  - 根本原因: dr_flac (DR_FLAC_NO_CRC) が各FLACファイルの最初のフレーム(4096サンプル)しかデコードしない
  - SpCA→FLAC変換後、フレーム1以降のCRC-8ヘッダが壊れており、dr_flacがフレーム境界を検出できない
  - find_frame_starts() もCRC-8有効なフレームを1つしか発見できない
  - その1フレーム全体(audio_start〜EOF)のCRC-16チェック → 899/1024ファイルでNG
  - CRC-16 NG → mute_bad_frames で最初の4096サンプルをゼロ埋め → 全PCMがゼロ → 「全サンプル同一値」でスキップ
  - CRC-16 pass: 125件 → 成功ロード132件とほぼ一致（差分はトリミング後の生存分）
  - 比較: Rhodes Classic は多フレーム構造のため部分ミュートで大部分が生存
  - ffmpeg も vel000/vel126 両方でフレーム1以降のデコードに失敗（SpCA CRC破損が原因）
次のTODO:
  - 修正案1: CRC-16 NG時にミュートせずデコード結果をそのまま使う（dr_flac NO_CRCで正常デコードできている）
  - 修正案2: find_frame_starts でCRC-8チェックを緩和（CRC-8なしでフレーム境界検出 → フレーム単位CRC-16判定）
  - 修正案3: extract_samples.py でSpCAのCRC-8を正しく再計算してFLAC出力
---

---
2026-03-06 18:51
作業項目: Wurlitzer XOR暗号化修正 — extract_samples.py 修正 + 再抽出
追加機能の説明:
  - Wurlitzer 200A の SpCA は XOR 暗号化されていた（encrypted=False が誤り）
  - extract_samples.py: wurl200a モードを encrypted=True に変更
  - _find_encrypted_frame1: expected_byte2 パラメータ追加（偽sync排除）
  - frame0 の byte2 (bs_code/sr_code) と一致するフレームのみマッチ
  - 偽マッチ: pos=4095 rot=3 byte2=0x28 (576samples/32kHz) → 排除
  - 正しいマッチ: pos=8969 rot=0 byte2=0xc9 (4096samples/44100Hz)
決定事項:
  - 全1024件の FLAC 再抽出成功
  - Pi デプロイ後: 132→1013 サンプルのロードに成功（7.8倍改善）
  - ロード時間: 5.1秒（キャッシュ再生成含む）
  - 起動完了確認、演奏テスト待ち
次のTODO: PG2に切り替えてWurlitzer演奏テスト
---

---
2026-03-06 21:39
作業項目: Wurlitzer 演奏テスト確認
追加機能の説明: なし
決定事項: ユーザー確認OK — Wurlitzer の音が正常に出ている
次のTODO: コミット・プッシュ
---

---
2026-03-06 21:45
作業項目: Space エフェクト切替 — Tape Echo / Room Reverb / Plate Reverb
追加機能の説明:
  - CC75-77 のディレイスロットを「Space エフェクト」に拡張
  - CC85 で切替: 0-42=Tape Echo, 43-84=Room Reverb, 85-127=Plate Reverb
  - 共通CC: CC76=Decay/Feedback, CC77=Wet
  - Room Reverb: Schroeder reverb (4 comb + 2 allpass)
  - Plate Reverb: 高密度反射 (6 allpass chain)
決定事項: 実装開始
次のTODO: 設計→実装→ビルド→テスト
---

---
2026-03-06 21:50
作業項目: CC番号整列
追加機能の説明:
  - Space Mode Select を CC85→CC75 に移動
  - Space パラメータを CC76-78 に（Time/Size, Decay/FB, Wet）
  - Chorus を CC79-81 に（Rate, Depth, Wet）
決定事項:
  - CC1-2: Tremolo
  - CC70-71: Overdrive
  - CC72-73: EQ
  - CC74: Release Time
  - CC75: Space Mode (Tape/Room/Plate)
  - CC76-78: Space (Time/Size, Decay/FB, Wet)
  - CC79-81: Chorus (Rate, Depth, Wet)
次のTODO: ビルド・デプロイ
---

---
2026-03-06 21:54
作業項目: オーバードライブ高品位化
追加機能の説明:
  - 2x オーバーサンプリング（エイリアシング除去）
  - 2段カスケードサチュレーション（プリ→パワー段）
  - キャビネットシミュレーション（Suitcase スピーカー風 BPF）
決定事項: 全部入りで実装・ユーザー確認OK
次のTODO: コミット
---

---
2026-03-06 21:59
作業項目: ペダルサンプル調査
追加機能の説明: なし（調査）
決定事項:
  - Rhodes .db にペダルノイズ専用サンプルは存在しない
  - pedal 128/129 ディレクトリはMIDI CCメタデータ(XML)のみ
  - wav プレフィックス: CLR(attack), CLRRel(release), CLRMch(mechanical), RR01_SL(sustain layers)
  - Keyscape はペダルノイズを独立サンプルとして収録していない
次のTODO: なし
---

---
2026-03-06 22:02
作業項目: ペダルノイズ フリーサンプル検索結果
追加機能の説明: なし（調査結果報告）
決定事項:
  - Freesound.org: "Piano Ambience (Pedal Presses, Bench Creaks, Hammer Strikes)" pack が有望
  - Freesound.org: "Piano sfx" by martats にもペダル音あり
  - CC0/Creative Commons ライセンスのものを候補に
次のTODO: ユーザーにサンプル候補を提示、選択後ダウンロード・統合
---

---
2026-03-06 22:05
作業項目: ペダルノイズ合成実装
追加機能の説明:
  - PedalNoise 構造体: フィルタードノイズバースト + 指数減衰エンベロープ
  - Pedal Down (CC64≥64): 150-400Hz帯域ノイズ、60ms減衰（低域トン）
  - Pedal Up (CC64<64): 400-1200Hz帯域ノイズ、35ms減衰（高域カチッ）
  - CC82: ペダルノイズ音量 (0=off, 127=max 0.30, default 0.15)
  - LCG乱数→1次LPF→1次HPF のバンドパスフィルタ
  - 外部サンプル不要、完全プログラム合成
決定事項:
  - Freesound はログイン必要で CLI ダウンロード困難→合成に切り替え
  - synth_engine.hpp に PedalNoise 構造体追加
  - synth_engine.cpp に実装 + CC64 状態変化でトリガー + mix() でミックス
  - Pi ビルド・デプロイ・起動OK
次のTODO: ペダル踏んでテスト
---

---
2026-03-06 22:09
作業項目: ペダルノイズ AR エンベロープ + CC コントロール
追加機能の説明:
  - AR (Attack-Release) エンベロープに拡張（即立ち上がり→減衰 から アタック→リリース に）
  - CC82: ペダルノイズ音量 (0=off, 127=max 0.30, default 0.15)
  - CC83: ペダルノイズ アタック (0=即, 127=30ms, default 2ms)
  - CC84: ペダルノイズ リリース (0=10ms, 127=150ms, default 60ms)
  - Pedal Up はリリース時間の60%で短めに自動調整
決定事項:
  - PedalNoise に Phase enum (IDLE/ATTACK/RELEASE) 追加
  - attack_ms, release_ms をメンバとして保持、trigger時にrateを計算
  - Pi ビルド・デプロイ・起動OK
次のTODO: ペダル踏んでテスト、音質調整
---

---
2026-03-06 22:15
作業項目: ペダル音を弦共鳴 + 機械クリックに分離
追加機能の説明:
  - PedalNoise を PedalClick + PedalResonance に分離
  - Pedal Down: 弦共鳴（12パーシャル加算合成、A1〜C5のピアノ弦周波数）
    - 低域ほど強く高域ほど弱い自然な倍音構成
    - ゆっくりアタック→ペダルUp までサステイン→リリース
  - Pedal Up: 機械クリック（フィルタードノイズ 400-1200Hz）+ 共鳴リリース
  - CC82: 音量, CC83: 共鳴アタック(0-200ms), CC84: 共鳴リリース(100-800ms)
決定事項:
  - PedalResonance: Phase (IDLE/ATTACK/SUSTAIN/RELEASE)
  - Bhaskara I sin近似で高速加算合成（12正弦波）
  - Pi ビルド・デプロイ・起動OK
次のTODO: ペダル踏んでテスト
---

---
2026-03-06 22:25
作業項目: ペダル共鳴を現在発音中ノートベースに変更
追加機能の説明:
  - 固定和音(A1-C5)→現在鳴っているノートの周波数で共鳴
  - 各ノートの基音 + 2倍音(oct) + 3倍音(5th+oct) の3パーシャル
  - ノートなし時は環境共鳴（55/110/82.4Hz の3本）
  - MAX_PARTIALS=24（8ノート×3パーシャル）
  - trigger_down() に active_notes 配列を渡す設計
決定事項:
  - CC64 ハンドラで Voice 配列をスキャンして発音中ノートを収集
  - Pi ビルド・デプロイ・起動OK
次のTODO: ペダル踏んでテスト
---

---
2026-03-06 22:28
作業項目: ペダル共鳴とクリックを別CC でコントロール
追加機能の説明:
  - 共鳴 (CC82-84): CC82=音量, CC83=アタック(0-200ms), CC84=リリース(100-800ms)
  - クリック (CC85-87): CC85=音量, CC86=アタック(0-30ms), CC87=リリース(10-150ms)
  - pedal_volume_ を resonance_vol_ と click_vol_ に分離
決定事項: Pi ビルド・デプロイ・起動OK
次のTODO: テスト
---

---
2026-03-06 22:30
作業項目: 全CCマッピング一覧出力
追加機能の説明: なし（情報提供）
決定事項: なし
次のTODO: なし
---

---
2026-03-06 22:32
作業項目: Space モードに OFF 追加・デフォルト化
追加機能の説明:
  - SpaceMode::OFF を追加、デフォルトに設定
  - CC75: 0-31=Off, 32-63=Tape Echo, 64-95=Room, 96-127=Plate
  - space_wet_ デフォルト 0.0 に変更
決定事項: Pi ビルド・デプロイ・起動OK
次のTODO: なし
---

---
2026-03-06 22:35
作業項目: CC7 Volume + CC11 Expression 実装
追加機能の説明:
  - CC7 (Volume): マスター音量 0〜1, デフォルト 1.0
  - CC11 (Expression): 表現用音量 0〜1, デフォルト 1.0
  - mix() でマスターゲイン = volume_ × expression_ をクリッピング前に適用
決定事項: Pi ビルド・デプロイ・起動OK
次のTODO: なし
---

---
2026-03-06 22:37
作業項目: Drive時の低音消失を修正
追加機能の説明:
  - キャビネットHPF: 200Hz/-24dB → 80Hz/-12dB に緩和
  - 原因: HPF 200Hzがピアノ低音域(A0=27Hz〜C4=261Hz)を大幅カットしていた
決定事項: Pi ビルド・デプロイ・起動OK
次のTODO: テスト
---

---
2026-03-06 22:39
作業項目: Drive音量一定化（自動ゲイン補正）
追加機能の説明:
  - 旧: saturate(x*d)/d → drive上げると音量低下
  - 新: saturate(x*d) + 自動compensation
  - 基準レベル0.3を2段サチュレーションに通し、出力の逆数で補正
  - drive=1: compensation≈1, drive=8: compensationで音量維持
決定事項: Pi ビルド・デプロイ・起動OK
次のTODO: テスト
---

---
2026-03-06 22:42
作業項目: コミット・プッシュ
追加機能の説明: commit 0149167
決定事項: push完了
次のTODO: なし
---

---
2026-03-06 23:05
作業項目: BLE リモートコントロール実装開始 (Phase 1-3)
追加機能の説明:
  - StatusReporter (UNIX domain socket) で elepiano ステータスを外部公開
  - ble-bridge (Python/dbus-next) で BLE GATT サービス提供
  - ALSA Seq 仮想ポート経由で CC/PC を elepiano に注入
  - iOS SwiftUI アプリで BLE 接続・CC スライダー操作
決定事項:
  - GATT Service UUID: e1e00000-0001-4000-8000-00805f9b34fb
  - Status/CC Control/Program Change/Audio Device/Batch CC の5キャラクタリスティック
  - synth_engine.hpp に getter 追加、StatusReporter 新規作成
次のTODO: 実装→ビルド→テスト
---

---
2026-03-06 23:15
作業項目: BLE リモートコントロール 全ファイル確認・ビルド・Xcodeプロジェクト作成
追加機能の説明:
  - 全作成済みファイルのコードレビュー・整合性確認
  - C++ ローカルビルド確認
  - iOS Xcode プロジェクト作成 (elepiano-remote.xcodeproj)
決定事項:
  - C++: status_reporter.hpp/cpp, synth_engine getter, main.cpp StatusReporter統合 すべて正常
  - BLE: ble_bridge.py, gatt_service.py, alsa_midi_sender.py, status_monitor.py すべて正常
  - iOS: 7 Swift ファイル + Info.plist すべて正常
次のTODO: ローカルビルド確認 → Xcode プロジェクト生成
---

---
2026-03-06 23:21
作業項目: iOS アプリ iPhone 実機デプロイ
追加機能の説明: なし
決定事項: Development Team 設定 → 実機ビルド・インストール
次のTODO: デプロイ実行
---

---
2026-03-06 23:25
作業項目: Pi5 SSH接続トラブルシュート
追加機能の説明: なし
決定事項: Pi5への接続確認・BLEブリッジ準備
次のTODO: 接続確認
---

---
2026-03-06 23:34
作業項目: iOS BLE接続後の動作デバッグ
追加機能の説明: なし
決定事項: BLE接続はできたがUIが遷移しない — 原因調査
次のTODO: Pi側ログ確認 + iOS側の問題切り分け
---

---
2026-03-06 23:40
作業項目: BLE CC/PC 送受信確認完了
追加機能の説明:
  - alsa_midi_sender.py: elepiano自動接続追加（aconnect不要に）
  - ble_bridge.py: CC/PCログ追加
  - gatt_service.py: ObjectManager実装（BlueZ RegisterApplication対応）
  - GattService1実装追加
決定事項:
  - BLE→GATT→ble-bridge→ALSA MIDI→elepiano のパイプライン動作確認
  - CC7 (Volume), PC (Program Change) の送受信をログで確認
  - iOS CCControlView 表示OK、Status Notify (Rhodes/Wurlitzer) 受信OK
次のTODO: 弾きながらスライダー操作でエフェクト変化を確認
---

---
2026-03-06 23:50
作業項目: ALSA MIDI CC送信バグ修正 — E2E動作確認完了
追加機能の説明:
  - alsa_midi_sender.py 根本修正:
    1. snd_seq_ev_ctrl_t 構造体修正 (c_uint→c_uint8+pad+c_uint32+c_int32 = 12B)
    2. dest を SUBSCRIBERS(253:253) → elepiano直接(128:0) に変更
    3. snd_seq_connect_to API で接続
    4. flags = 0 (TICK + FIXED) に修正
決定事項:
  - 根本原因: SUBSCRIBERS 宛の output_direct が rc=-2 (ENOENT) で失敗していた
  - dest に直接 elepiano の client:port を指定することで解決
  - iPhone CC1 (Tremolo) → ble-bridge → elepiano に正常到達確認
次のTODO: 弾きながらスライダー操作で音の変化を確認、コミット
---

---
2026-03-06 23:54
作業項目: iOS アプリにサービス状態表示 + リスタート機能を追加
追加機能の説明:
  - ble-bridge: elepiano/ble-bridge の systemd サービス状態を Status JSON に含める
  - ble-bridge: CommandCharacteristic (UUID 0006) 追加 — restart コマンド受付
  - iOS: StatusView に起動状態インジケーター表示（not running / loading / ready）
  - iOS: サービスリスタートボタン追加
  - iOS: BLEManager に commandCharacteristic + sendCommand 追加
決定事項:
  - elepiano 接続状態は ble-bridge が判定（UNIX socket 接続可否）
  - restart は ble-bridge が systemctl restart elepiano を実行
  - C++ 側変更なし（status JSON は既存のまま）
次のTODO: 実装→ビルド→テスト
---

---
2026-03-07 00:54
作業項目: GitHub Issues 評価・クローズ
追加機能の説明: なし（評価のみ）
決定事項:
  - #29 (FxChain データ競合) close — SpscQueue 導入で修正済み
  - #21 (BLE-MIDI スロットリング) close — iOS ccMinInterval=0.025s で実装済み
  - #27 (SampleDB::find RT-safe) close — reserve(16) + clear() で RT-safe
  - #20 (パストラバーサル) close — weakly_canonical + relative で保護済み
  - #19 (SpCA XOR security) close — runtime に SpCA 処理なし、不要
  - #28 (analyze_noise.py メモリ) close — 現状で十分
  - #31 (PCM cache int16) — 低優先（メモリ逼迫時のみ）
  - #30 (sanitize_flac) — 低優先（tools 側、堅牢性は十分）
  - #26 (WAV dump fwrite) — 低優先（標準無効、使用時のみ改善推奨）
次のTODO: 修正済み・不要 issue をクローズ
---

---
2026-03-07 00:56
作業項目: #31 PCM キャッシュ int16 化
追加機能の説明:
  - CACHE_VERSION 5→6
  - save_cache: float→int16 変換して書き込み（ディスク容量 50% 削減）
  - load_cache: int16→float 変換して読み込み
  - SampleData::pcm は float のまま（オーディオスレッド互換）
決定事項:
  - Rhodes: 1.4GB→708MB, Wurlitzer: 1.3GB→643MB (50%削減)
  - 3音色復活が可能（合計~2.8GB < Pi5 8GB）
  - Pi ビルド・デプロイ・キャッシュ再生成完了
次のTODO: 3音色構成テスト
---

---
2026-03-07 01:00
作業項目: Vintage Vibe 復活 — 3 音色構成
追加機能の説明:
  - elepiano.service に --pg data/vintage-vibe-ep/samples.json 追加
  - iOS CCControlView に "Vintage Vibe" ボタン追加（PG3）
決定事項:
  - 3音色で起動成功: PG1=Rhodes, PG2=Wurlitzer, PG3=Vintage Vibe
  - メモリ: used=5.5GB, free=174MB, swap=33MB — ギリギリだが安定
  - キャッシュ: Rhodes 708MB + Rel 27MB + Wurl 643MB + VV 1.2GB = 2.6GB (int16)
  - 旧float32だと 5.5GB → OOM だった。int16化で3音色復活
  - iOS アプリ更新・iPhone デプロイ完了
次のTODO: PG3 に切り替えて Vintage Vibe 演奏テスト
---

---
2026-03-07 01:06
作業項目: Wurlitzer → Rhodes LA Custom 入れ替え
追加機能の説明:
  - Rhodes LA Custom (2985 attack + 1408 release) FLAC を Pi に転送 (2.0GB + 47MB)
  - elepiano.service: PG2 を Wurlitzer → LA Custom + release に変更
  - iOS CCControlView: "Wurlitzer" → "LA Custom" に変更
決定事項:
  - PG1=Rhodes Classic, PG2=Rhodes LA Custom, PG3=Vintage Vibe
  - 3音色で起動成功: used=4.5GB, available=3.5GB — 安定
  - LA Custom attack 2985→キャッシュ生成完了、release 1201サンプル
  - iOS アプリ更新・デプロイ完了
次のTODO: PG2 に切り替えて LA Custom 演奏テスト
---

---
2026-03-07 01:12
作業項目: LA Custom サンプル全数ロード修正
追加機能の説明:
  - MAX_SAMPLE_FRAMES 441000→882000 (10s→20s) — 長いサンプルがカットされていた
  - flac_decoder.cpp: CRC-16 エラーが50%超なら SpCA仕様と判断しミュートスキップ
  - 根本原因: SpCA の CRC-16 が意図的に壊されている（暗号化なしでも）
  - mute_bad_frames が全フレームゼロ化→「全サンプル同一値」でスキップされていた
  - CACHE_VERSION 6→8
決定事項:
  - LA Custom: 142→2985 全数ロード成功
  - LA Custom release: 1201→1760 全数ロード成功
  - 3音色で起動OK (used=6.7GB, swap=874MB)
次のTODO: PG2 に切り替えて LA Custom 演奏テスト
---
