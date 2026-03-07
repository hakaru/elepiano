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
        Group {
            if ble.isConnected {
                LandscapeCCView()
            } else {
                ConnectionView()
            }
        }
        .preferredColorScheme(.dark)
    }
}

// Force landscape orientation
struct LandscapeModifier: ViewModifier {
    func body(content: Content) -> some View {
        content
            .onAppear {
                let windowScene = UIApplication.shared.connectedScenes.first as? UIWindowScene
                windowScene?.requestGeometryUpdate(.iOS(interfaceOrientations: .landscape))
            }
    }
}
