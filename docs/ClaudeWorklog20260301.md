---
2026-03-01 16:38
作業項目: Keyscape STEAMデータ解析の調査開始
追加機能の説明: /Volumes/Media/AudioLibraries/Spectrasonics/STEAM/Keyscape のデータ構造を調査する
決定事項: まずディレクトリ構造とファイル種別を確認する
次のTODO: ファイル形式の特定、解析方針の検討
---

---
2026-03-01 16:42
作業項目: Keyscape → elepiano 連携調査
追加機能の説明: ユーザーが選択肢4「elepiano連携」を選択。Keyscapeパッチ情報をelepianoで活用するための調査開始
決定事項: elepianoのコードベースを調査して連携ポイントを特定する
次のTODO: elepianoのプロジェクト構造確認、音源・パッチ管理の仕組み把握
---

---
2026-03-01 16:56
作業項目: SpCA直接デコードの模索
追加機能の説明: Spectrasonics STEAMのSpCAファイル形式を直接デコードする方法を調査する
決定事項: まずSTEAMディレクトリ内のSpCAファイルの実態を調べ、バイナリ構造を解析する
次のTODO: SpCAファイルのバイナリ先頭を確認、既知フォーマットとの照合
---

---
2026-03-01 17:24
作業項目: SpCA直接デコード - 完全解析成功
追加機能の説明: SpCAフォーマットの完全リバースエンジニアリング完了
決定事項:
  - SpCA = FLACに独自マジックバイト+XOR暗号化を施したラッパー
  - 構造: [SpCA magic] + [標準FLACメタデータ] + [XOR暗号化FLACフレーム]
  - XOR BASE_KEY = [0xef, 0x42, 0x12, 0xbc] の4通りローテーション
  - フレーム0はXORなし（CRC-16は意図的に壊れているがFFmpegは正常デコード）
  - フレーム1以降: 最初の4バイトがff f8 c9 0cになるローテーションを選択
  - 529フレーム全てCRC-16一致、24-bit PCMサンプルも正常 (0, -42, -168, -380...)
  - FFmpegで49.15秒の完全デコード確認
次のTODO: elepianoへの統合方針を決定（リアルタイムデコード or オフライン変換）
---

---
2026-03-01 17:26
作業項目: PiSound MIDI音源化の設計検討
追加機能の説明: elepianoをPiSound上でMIDI音源として動作させる構成を設計する
決定事項: PiSoundのハードウェア特性・ALSAインターフェース・MIDI→音声の処理フローを調査
次のTODO: elepianoの既存コードベース確認、PiSoundのMIDI/Audio仕様確認
---

---
2026-03-01 17:42
作業項目: PiSound MIDI音源化 実装開始
追加機能の説明: 設計計画に従いPhase1（サンプル抽出ツール）とPhase2（C++ MIDIシンセエンジン）を実装
決定事項:
  - tools/extract_samples.py: SpCAデコード＋FLAC/WAV変換＋JSONマップ生成
  - src/: C++シンセエンジン (sample_db, voice, synth_engine, midi_input, audio_output)
  - CMakeLists.txt: ALSA + FLAC依存ビルド設定
次のTODO: 各ファイルの実装、Mac上でのextract_samples.pyテスト
---

---
2026-03-01 17:51
作業項目: git commit & push
追加機能の説明: 実装済みファイルを全てコミットしてリモートにプッシュ
決定事項: コミット対象=tools/, src/, cmake/, CMakeLists.txt, data/rhodes/samples.json (FLACバイナリは除外)
次のTODO: Pi上でのビルド・実機テスト
---

---
2026-03-01 17:48
作業項目: Phase 1-3 実装完了・動作確認
追加機能の説明: 全コード実装、extract_samples.py の実行テスト完了
決定事項:
  - Pattern 1 サンプルは XOR 暗号化なし（SpCA magic→fLaC置換のみ）
  - Sustain サンプルは XOR 暗号化あり（フレーム1以降がXOR）
  - decode_spca: フレーム1先頭がff f8/f9ならXOR不要と判断するロジックを追加
  - data/rhodes/audio/ に 38本のFLAC出力成功 (44.1kHz/24bit/mono)
  - data/rhodes/samples.json 生成完了
  - C++シンセエンジン (sample_db/voice/synth_engine/midi_input/audio_output) 作成完了
  - CMakeLists.txt + cmake/pi-toolchain.cmake 作成完了
次のTODO:
  - Pi上でのCMakeビルド (apt install libasound2-dev libflac-dev nlohmann-json3-dev)
  - MIDI+ALSAの動作確認（virtual MIDIでMac上テスト可）
  - Sustain サンプル（XOR有り）も後で対応
---
