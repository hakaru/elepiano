import SwiftUI

struct StatusView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var showRestartConfirm = false

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
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
        if !ble.isConnected {
            Text(ble.isScanning ? "Scanning..." : "Not connected")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .onTapGesture { ble.startScan() }
        } else if status == nil {
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

// Compact status for landscape top bar
struct CompactStatusView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        HStack(spacing: 4) {
            let status = ble.status
            let isReady = status?.isReady ?? false
            let isEleConnected = status?.elepianoConnected ?? false

            Circle()
                .fill(isReady ? .green : (isEleConnected ? .orange : (ble.isConnected ? .yellow : .red)))
                .frame(width: 7, height: 7)

            if !ble.isConnected {
                Text(ble.isScanning ? "Scan..." : "No BLE")
                    .font(.system(size: 10))
                    .foregroundStyle(.secondary)
                    .onTapGesture { ble.startScan() }
            }
        }
    }
}
