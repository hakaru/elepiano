import SwiftUI

@main
struct ElepianoControlApp: App {
    @State private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            MainView()
                .environment(appState)
        }
    }
}
