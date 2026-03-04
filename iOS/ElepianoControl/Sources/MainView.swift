import SwiftUI

struct MainView: View {
    @Environment(AppState.self) private var state

    var body: some View {
        TabView {
            Tab("Connection", systemImage: "antenna.radiowaves.left.and.right") {
                DeviceListView()
            }

            Tab("FX", systemImage: "slider.horizontal.3") {
                if state.mode == .piano {
                    PianoFXView()
                } else {
                    OrganControlView()
                }
            }

            Tab("Presets", systemImage: "square.grid.2x2") {
                PresetView()
            }
        }
        .onAppear { state.start() }
    }
}
