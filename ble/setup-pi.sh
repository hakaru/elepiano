#!/bin/bash
# elepiano BLE Bridge セットアップスクリプト (Pi5 用)
# Usage: sudo bash setup-pi.sh

set -e

echo "=== elepiano BLE Bridge Setup ==="

# 1. Python 依存パッケージ
echo "[1/4] Installing Python dependencies..."
pip3 install --break-system-packages dbus-next 2>/dev/null || pip3 install dbus-next

# 2. elepiano.service に --status-socket を追加
echo "[2/4] Updating elepiano.service..."
SERVICE_FILE="/etc/systemd/system/elepiano.service"
if [ -f "$SERVICE_FILE" ]; then
    if ! grep -q "status-socket" "$SERVICE_FILE"; then
        sed -i 's|ExecStart=\(.*elepiano\)|ExecStart=\1 --status-socket /tmp/elepiano-status.sock|' "$SERVICE_FILE"
        echo "  Added --status-socket to elepiano.service"
    else
        echo "  --status-socket already configured"
    fi
fi

# 3. ble-bridge.service をインストール
echo "[3/4] Installing ble-bridge.service..."
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cp "$SCRIPT_DIR/ble-bridge.service" /etc/systemd/system/
# WorkingDirectory を実際のパスに更新
sed -i "s|WorkingDirectory=.*|WorkingDirectory=$SCRIPT_DIR|" /etc/systemd/system/ble-bridge.service
sed -i "s|ExecStart=.*|ExecStart=/usr/bin/python3 $SCRIPT_DIR/ble_bridge.py|" /etc/systemd/system/ble-bridge.service

systemctl daemon-reload
systemctl enable ble-bridge.service

# 4. Bluetooth 設定
echo "[4/4] Configuring Bluetooth..."
# Bluetooth が起動していることを確認
systemctl enable bluetooth
systemctl start bluetooth

# BLE advertising を有効にする
hciconfig hci0 up 2>/dev/null || true
hciconfig hci0 leadv 0 2>/dev/null || true

echo ""
echo "=== Setup complete ==="
echo "Restart services:"
echo "  sudo systemctl restart elepiano"
echo "  sudo systemctl start ble-bridge"
echo ""
echo "Check status:"
echo "  systemctl status elepiano"
echo "  systemctl status ble-bridge"
