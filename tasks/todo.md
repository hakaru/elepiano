# elepiano TODO

## レイテンシ改善（優先度順）

### すぐできる（リスクなし）
- [x] L1: CPUガバナーを `performance` に固定（ondemand→常時2.4GHz）✅ crontab永続化済み
- [x] L2: `threadirqs` カーネルパラメータ追加（割込みスレッド化）✅ 次回再起動で有効

### 効果大（要再起動・検証）
- [x] L3: PREEMPT_RT カーネル導入 ✅ linux-image-rpi-v8-rt 6.12.62, cyclictest Max=6μs
- [ ] L4: CPU隔離 `isolcpus=3 nohz_full=3 rcu_nocbs=3` でオーディオスレッド専用コア
  - elepiano 側で `taskset -c 3` or `pthread_setaffinity`

### 計測・検証
- [x] L5: `cyclictest` インストールして改善前後のジッター計測 ✅ Max=5μs, Avg=1μs (PREEMPT+performance)
- [x] L6: period_size=32 で underrun なく動くか確認（理論 ~1.3ms）✅ 安定動作確認
- [x] L7: サンプル先頭無音トリミング ✅ 92.9ms→0ms, 体感レイテンシ劇的改善

---

## 次のTODO候補

- [ ] systemd 自動起動サービス化
- [ ] 他音色追加（Wurlitzer 200A, Vintage Vibe EP）
- [ ] README.md 作成
- [ ] CPU隔離 (L4)
