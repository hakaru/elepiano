import SwiftUI

struct PresetView: View {
    @Environment(AppState.self) private var state
    @State private var showingSaveSheet = false
    @State private var newPresetName = ""

    var body: some View {
        NavigationStack {
            List {
                if state.presets.isEmpty {
                    ContentUnavailableView(
                        "No Presets",
                        systemImage: "square.grid.2x2",
                        description: Text("Tap + to save current settings")
                    )
                } else {
                    ForEach(Array(state.presets.enumerated()), id: \.element.id) { index, preset in
                        Button {
                            state.loadPreset(preset)
                        } label: {
                            HStack {
                                VStack(alignment: .leading) {
                                    Text(preset.name)
                                        .font(.headline)
                                    Text(preset.mode.rawValue)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                                Spacer()
                                Image(systemName: "play.fill")
                                    .foregroundStyle(.blue)
                            }
                        }
                        .tint(.primary)
                        .swipeActions(edge: .trailing) {
                            Button(role: .destructive) {
                                state.deletePreset(at: index)
                            } label: {
                                Label("Delete", systemImage: "trash")
                            }
                        }
                    }
                }
            }
            .navigationTitle("Presets")
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Button {
                        newPresetName = ""
                        showingSaveSheet = true
                    } label: {
                        Image(systemName: "plus")
                    }
                }
            }
            .alert("Save Preset", isPresented: $showingSaveSheet) {
                TextField("Name", text: $newPresetName)
                Button("Save") {
                    guard !newPresetName.isEmpty else { return }
                    state.savePreset(name: newPresetName)
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Enter a name for the preset")
            }
        }
    }
}
