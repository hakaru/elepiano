#!/usr/bin/env bash
# tools/redeploy_samples.sh — Mac → Pi5 サンプル再デプロイスクリプト
#
# 用途:
#   Pi5 上に残っている古い/壊れた FLAC ファイルをクリーンなものに差し替える。
#   "きゅーー" というデジタルノイズの原因となった壊れたファイルを完全に上書きする。
#
# 前提:
#   - Mac 側に flac コマンドが入っていること (brew install flac)
#   - Pi5 に SSH 鍵認証で接続できること (ssh hakaru@hakarupiano.local)
#   - Pi5 側に flac コマンドが入っていること (sudo apt install flac)
#
# 使い方:
#   bash tools/redeploy_samples.sh [オプション]
#
#   --host HOST      Pi5 のホスト名 or IP (デフォルト: hakarupiano.local)
#   --user USER      SSH ユーザー名        (デフォルト: hakaru)
#   --remote-dir DIR Pi5 上のプロジェクト root (デフォルト: ~/elepiano)
#   --local-dir DIR  Mac 上の data/ ディレクトリ (デフォルト: スクリプトの ../data)
#   --dry-run        実際には転送せず rsync の --dry-run で差分確認のみ
#   --no-verify-mac  Mac 側の整合性チェックをスキップ
#   --no-verify-pi   Pi5 側のデプロイ後検証をスキップ

set -euo pipefail

# ─── デフォルト設定 ──────────────────────────────────────────
PI_HOST="${PI_HOST:-hakarupiano.local}"
PI_USER="${PI_USER:-hakaru}"
REMOTE_DIR="${REMOTE_DIR:-~/elepiano}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_DATA_DIR="${LOCAL_DATA_DIR:-$(dirname "$SCRIPT_DIR")/data}"
DRY_RUN=false
VERIFY_MAC=true
VERIFY_PI=true

# ─── 引数パース ──────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)       PI_HOST="$2";         shift 2 ;;
        --user)       PI_USER="$2";         shift 2 ;;
        --remote-dir) REMOTE_DIR="$2";      shift 2 ;;
        --local-dir)  LOCAL_DATA_DIR="$2";  shift 2 ;;
        --dry-run)    DRY_RUN=true;         shift   ;;
        --no-verify-mac) VERIFY_MAC=false;  shift   ;;
        --no-verify-pi)  VERIFY_PI=false;   shift   ;;
        -h|--help)
            head -30 "$0" | grep "^#" | sed 's/^# \?//'
            exit 0 ;;
        *)
            echo "不明なオプション: $1" >&2
            exit 1 ;;
    esac
done

PI_TARGET="${PI_USER}@${PI_HOST}"
REMOTE_DATA_DIR="${REMOTE_DIR}/data"

# ─── カラー出力 ──────────────────────────────────────────────
RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[0;33m'
BLU='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${BLU}[INFO]${NC}  $*"; }
ok()    { echo -e "${GRN}[OK]${NC}    $*"; }
warn()  { echo -e "${YLW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# ─── ステップ 1: Mac 側 FLAC 整合性チェック ─────────────────
step1_check_mac() {
    info "=== Step 1: Mac 側サンプル整合性チェック ==="

    if ! command -v flac &>/dev/null; then
        warn "flac コマンドが見つかりません。スキップします。"
        warn "インストール: brew install flac"
        return 0
    fi

    local flac_files
    flac_files=$(find "$LOCAL_DATA_DIR" -name "*.flac" 2>/dev/null)

    if [[ -z "$flac_files" ]]; then
        warn "data/ に .flac ファイルが見つかりません。"
        warn "extract_samples.py でサンプルを先に生成してください。"
        return 0
    fi

    local total broken=0
    total=$(echo "$flac_files" | wc -l | tr -d ' ')
    info "チェック対象: ${total} ファイル"

    local broken_list=()
    while IFS= read -r f; do
        # ファイルサイズ 0 チェック
        if [[ ! -s "$f" ]]; then
            error "空ファイル: $f"
            broken_list+=("$f")
            ((broken++)) || true
            continue
        fi

        # FLAC ヘッダーチェック（先頭4バイトが fLaC か）
        local magic
        magic=$(xxd -l4 -p "$f" 2>/dev/null || true)
        if [[ "$magic" != "664c6143" ]]; then
            error "FLACマジック不正: $f (先頭: $magic)"
            broken_list+=("$f")
            ((broken++)) || true
            continue
        fi

        # flac -t でストリーム検証
        if ! flac --totally-silent -t "$f" 2>/dev/null; then
            error "FLAC検証失敗: $f"
            broken_list+=("$f")
            ((broken++)) || true
        fi
    done <<< "$flac_files"

    if [[ $broken -eq 0 ]]; then
        ok "全 ${total} ファイルの整合性OK"
    else
        error "${broken}/${total} ファイルが壊れています"
        echo ""
        warn "壊れたファイル一覧:"
        for f in "${broken_list[@]}"; do
            echo "  $f"
        done
        echo ""
        warn "extract_samples.py で再生成してから再実行してください。"
        return 1
    fi
}

# ─── ステップ 2: Pi5 への rsync 転送 ────────────────────────
step2_rsync() {
    info "=== Step 2: Pi5 へ rsync 転送 ==="
    info "転送元: ${LOCAL_DATA_DIR}/"
    info "転送先: ${PI_TARGET}:${REMOTE_DATA_DIR}/"

    local rsync_opts=(
        --archive           # -a: パーミッション/タイムスタンプ/シンボリックリンク保持
        --checksum          # サイズ/タイムスタンプでなくチェックサムで差分判定（壊れたファイルも上書き）
        --compress          # SSH 転送時に圧縮（FLAC は既圧縮なので効果は限定的だが有効化）
        --progress          # 進捗表示
        --human-readable    # 転送サイズを人間が読みやすい形式で表示
        --stats             # 転送完了後にサマリを表示
        --delete            # Pi5 側に Mac にないファイルが残っていれば削除
    )

    if [[ "$DRY_RUN" == "true" ]]; then
        warn "DRY RUN モード: 実際には転送しません"
        rsync_opts+=(--dry-run)
    fi

    # Pi5 側のディレクトリを事前に作成
    if [[ "$DRY_RUN" != "true" ]]; then
        ssh "${PI_TARGET}" "mkdir -p '${REMOTE_DATA_DIR}'"
    fi

    rsync "${rsync_opts[@]}" \
        -e "ssh -o StrictHostKeyChecking=accept-new" \
        "${LOCAL_DATA_DIR}/" \
        "${PI_TARGET}:${REMOTE_DATA_DIR}/"

    ok "rsync 完了"
}

# ─── ステップ 3: Pi5 側デプロイ後検証 ───────────────────────
step3_verify_pi() {
    info "=== Step 3: Pi5 側デプロイ後検証 ==="

    local remote_script
    remote_script=$(cat <<'REMOTE_EOF'
set -euo pipefail
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[0;33m'; BLU='\033[0;34m'; NC='\033[0m'
info()  { echo -e "${BLU}[PI]${NC}   $*"; }
ok()    { echo -e "${GRN}[PI:OK]${NC} $*"; }
warn()  { echo -e "${YLW}[PI:W]${NC} $*"; }
error() { echo -e "${RED}[PI:E]${NC} $*" >&2; }

DATA_DIR="$1"

# ファイル数確認
total=$(find "$DATA_DIR" -name "*.flac" 2>/dev/null | wc -l)
info "Pi5 上の FLAC ファイル数: $total"

if [[ $total -eq 0 ]]; then
    warn "FLACファイルが見つかりません: $DATA_DIR"
    exit 0
fi

# flac コマンド確認
if ! command -v flac &>/dev/null; then
    warn "flac コマンドなし。インストールします..."
    sudo apt-get install -y flac -q 2>/dev/null || warn "インストール失敗。検証スキップ。"
fi

if ! command -v flac &>/dev/null; then
    warn "flac コマンドが使えないため Pi5 側検証をスキップ"
    info "総ファイル数: $total (検証なし)"
    exit 0
fi

# サンプリング検証（全ファイルは時間がかかるため各楽器から最大5ファイル）
broken=0
checked=0
for dir in "$DATA_DIR"/*/audio/; do
    [[ -d "$dir" ]] || continue
    mapfile -t sample_files < <(find "$dir" -name "*.flac" | shuf | head -5)
    for f in "${sample_files[@]}"; do
        if ! flac --totally-silent -t "$f" 2>/dev/null; then
            error "検証失敗: $f"
            ((broken++)) || true
        fi
        ((checked++)) || true
    done
done

if [[ $broken -eq 0 ]]; then
    ok "サンプリング検証 ${checked}/${total} ファイル: 全OK"
    info "elepiano を再起動してテストしてください:"
    info "  cd ~/elepiano/build && ./elepiano data/rhodes-classic/samples.json"
else
    error "${broken}/${checked} ファイルが Pi5 上でも壊れています"
    error "Mac 側のソースファイルを再確認してください"
    exit 1
fi
REMOTE_EOF
)

    ssh -o StrictHostKeyChecking=accept-new "${PI_TARGET}" \
        bash -s -- "${REMOTE_DATA_DIR}" <<< "$remote_script"

    ok "Pi5 側検証完了"
}

# ─── メイン ─────────────────────────────────────────────────
main() {
    echo ""
    echo "========================================"
    echo " elepiano サンプル再デプロイ"
    echo "  Pi5  : ${PI_TARGET}"
    echo "  転送先: ${REMOTE_DATA_DIR}"
    echo "  転送元: ${LOCAL_DATA_DIR}"
    if [[ "$DRY_RUN" == "true" ]]; then
        echo "  モード: DRY RUN (実転送なし)"
    fi
    echo "========================================"
    echo ""

    # data/ ディレクトリ存在確認
    if [[ ! -d "$LOCAL_DATA_DIR" ]]; then
        error "data/ ディレクトリが見つかりません: ${LOCAL_DATA_DIR}"
        exit 1
    fi

    # SSH 接続確認
    info "Pi5 への SSH 接続確認..."
    if ! ssh -o StrictHostKeyChecking=accept-new \
             -o ConnectTimeout=10 \
             "${PI_TARGET}" "echo ok" &>/dev/null; then
        error "Pi5 に SSH 接続できません: ${PI_TARGET}"
        error "以下を確認してください:"
        error "  - Pi5 が起動しているか"
        error "  - SSH 鍵認証が設定されているか"
        error "  - Pi5 のホスト名/IP が正しいか (--host で上書き可)"
        exit 1
    fi
    ok "SSH 接続OK"

    # ステップ実行
    if [[ "$VERIFY_MAC" == "true" ]]; then
        step1_check_mac || exit 1
    else
        warn "Mac 側整合性チェックをスキップ"
    fi

    step2_rsync

    if [[ "$DRY_RUN" == "true" ]]; then
        warn "DRY RUN のため Pi5 側検証はスキップ"
        echo ""
        ok "DRY RUN 完了。--dry-run を外して実際に転送してください。"
    else
        if [[ "$VERIFY_PI" == "true" ]]; then
            step3_verify_pi
        else
            warn "Pi5 側検証をスキップ"
        fi

        echo ""
        ok "=== 再デプロイ完了 ==="
        echo ""
        info "次のステップ:"
        info "  1. Pi5 上で elepiano を再起動してください:"
        info "     ssh ${PI_TARGET}"
        info "     cd ${REMOTE_DIR}/build"
        info "     ./elepiano ../data/rhodes-classic/samples.json"
        info ""
        info "  2. 'きゅーー' ノイズが解消されたか確認してください。"
    fi
}

main "$@"
