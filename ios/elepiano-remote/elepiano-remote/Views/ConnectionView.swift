import SwiftUI

struct ConnectionView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        VStack(spacing: 24) {
            Image(systemName: "pianokeys")
                .font(.system(size: 60))
                .foregroundStyle(.secondary)

            Text("elepiano Remote")
                .font(.largeTitle)
                .fontWeight(.bold)

            if ble.isScanning {
                ProgressView("Scanning...")
                    .padding()
            } else {
                Button {
                    ble.startScan()
                } label: {
                    Label("Scan for elepiano", systemImage: "antenna.radiowaves.left.and.right")
                        .font(.headline)
                        .padding()
                        .frame(maxWidth: .infinity)
                        .background(.blue)
                        .foregroundColor(.white)
                        .cornerRadius(12)
                }
                .padding(.horizontal, 40)
            }

            Text("Make sure elepiano and ble-bridge are running on the Pi")
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal)
        }
        .navigationTitle("Connect")
    }
}
