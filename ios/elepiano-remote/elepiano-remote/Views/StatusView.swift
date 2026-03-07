import SwiftUI

struct StatusView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var showRestartConfirm = false

    var body: some View {
        VStack(spacing: 0) {
            // メインステータスバー
            HStack(spacing: 12) {
                // 接続状態インジケーター
                statusIndicator

                if let status = ble.status, status.isReady {
                    Label(status.name ?? "---", systemImage: "pianokeys")
                        .font(.headline)

                    Spacer()

                    Label("\(status.voices ?? 0)", systemImage: "waveform")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)

                    if let underruns = status.underruns, underruns > 0 {
                        Label("\(underruns)", systemImage: "exclamationmark.triangle")
                            .font(.caption)
                            .foregroundStyle(.red)
                    }
                } else {
                    statusMessage
                    Spacer()
                }

                // リスタートメニュー
                Menu {
                    Button {
                        ble.sendCommand("restart_elepiano")
                    } label: {
                        Label("Restart elepiano", systemImage: "arrow.clockwise")
                    }
                    Button {
                        ble.sendCommand("restart_ble_bridge")
                    } label: {
                        Label("Restart BLE Bridge", systemImage: "antenna.radiowaves.left.and.right")
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                        .font(.title3)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
            .background(.ultraThinMaterial)
        }
    }

    @ViewBuilder
    private var statusIndicator: some View {
        let status = ble.status
        let isReady = status?.isReady ?? false
        let isConnected = status?.elepianoConnected ?? false

        Circle()
            .fill(isReady ? .green : (isConnected ? .orange : .red))
            .frame(width: 10, height: 10)
    }

    @ViewBuilder
    private var statusMessage: some View {
        let status = ble.status
        if status == nil {
            Text("Waiting for status...")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        } else if !(status?.elepianoConnected ?? false) {
            Text("elepiano not running")
                .font(.subheadline)
                .foregroundStyle(.red)
        } else {
            Text("Loading...")
                .font(.subheadline)
                .foregroundStyle(.orange)
        }
    }
}
