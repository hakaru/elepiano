import SwiftUI

@main
struct ElepianoRemoteApp: App {
    @StateObject private var bleManager = BLEManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(bleManager)
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        NavigationStack {
            if ble.isConnected {
                CCControlView()
            } else {
                ConnectionView()
            }
        }
    }
}
