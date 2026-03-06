import SwiftUI

struct StatusView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        if let status = ble.status {
            HStack(spacing: 16) {
                Label(status.name, systemImage: "pianokeys")
                    .font(.headline)

                Spacer()

                Label("\(status.voices)", systemImage: "waveform")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                if status.underruns > 0 {
                    Label("\(status.underruns)", systemImage: "exclamationmark.triangle")
                        .font(.caption)
                        .foregroundStyle(.red)
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
            .background(.ultraThinMaterial)
        }
    }
}
