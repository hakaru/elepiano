# elepiano Open Issues 解決計画

作成日: 2026-03-04

## 実行戦略

- グループ A/C/D/E は独立ファイル → **並列実行**
- グループ B は `flac_decoder.cpp` 内で変更行が重複 → **直列** (#18→#9→#7→#13)
- グループ F (#12) はグループ B・E 完了後

---

## グループ A: sample_db.cpp セキュリティ修正

- [x] #10: パストラバーサルガードを `std::filesystem` ベースに修正 → `35af3ac`
- [x] #17: samples.json サイズ制限・サンプル数上限を追加 → `ec49937`

## グループ B: flac_decoder.cpp 修正（直列）

- [x] #18: read_file_bytes() に tellg() 失敗ガード・サイズ上限追加 → `481af1c`
- [x] #9: SpCA XOR の sr_code==9 ハードコードを緩い条件に修正 → `4c1200c`
- [x] #7: multi-channel PCM バッファサイズ計算に channels を反映 → `4c1200c`
- [x] #13: 通常FLAC のストリーミングデコード追加・二重デコード排除 → `4c1200c`

## グループ C: midi_input.cpp セキュリティ修正

- [x] #16: ALSA poll descriptor count にバリデーション追加 → `f54a266`

## グループ D: main.cpp 修正

- [x] #8: デフォルト ALSA デバイスを "default" に変更 → `1b430c2`
- [x] #14: MIDI ログを条件付き出力に変更（デフォルト無効） → `b586744`

## グループ E: Python ツール修正

- [x] #11: extract_samples.py の CRC デッドコード削除 → `5378723`
- [x] #15: extract_samples.py の並列化・コピー削減 → `69c5c97`

## グループ F: SpCA XOR リファクタ（B・E 完了後）

- [x] #12: C++/Python の SpCA XOR ロジック統合 → `e7e6916`

---

## 結果

全12件の issue を10コミットで修正完了。push 後に GitHub 上で自動クローズされる。
