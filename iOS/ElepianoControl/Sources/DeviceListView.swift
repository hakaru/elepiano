import SwiftUI
import MIDIKitIO

struct DeviceListView: View {
    @Environment(AppState.self) private var state
    @State private var showingBLEPairing = false

    var body: some View {
        NavigationStack {
            List {
                // 接続状態
                Section {
                    if state.isConnected, let name = state.connectedDeviceName {
                        HStack {
                            Image(systemName: "checkmark.circle.fill")
                                .foregroundStyle(.green)
                            Text(name)
                            Spacer()
                            Button("Disconnect") { state.disconnect() }
                                .buttonStyle(.bordered)
                                .tint(.red)
                        }
                    } else {
                        HStack {
                            Image(systemName: "circle")
                                .foregroundStyle(.secondary)
                            Text("Not connected")
                                .foregroundStyle(.secondary)
                        }
                    }
                } header: {
                    Text("Status")
                }

                // モード切替
                Section {
                    @Bindable var s = state
                    Picker("Engine", selection: $s.mode) {
                        ForEach(EngineMode.allCases, id: \.self) { mode in
                            Text(mode.rawValue).tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)
                } header: {
                    Text("Mode")
                }

                // デバイス一覧
                Section {
                    if state.availableInputs.isEmpty {
                        Text("No MIDI devices found")
                            .foregroundStyle(.secondary)
                    } else {
                        ForEach(state.availableInputs, id: \.uniqueID) { endpoint in
                            Button {
                                state.connect(to: endpoint)
                            } label: {
                                HStack {
                                    Image(systemName: "pianokeys")
                                    Text(endpoint.displayName)
                                    Spacer()
                                    if state.connectedEndpointID == endpoint.uniqueID {
                                        Image(systemName: "checkmark")
                                            .foregroundStyle(.blue)
                                    }
                                }
                            }
                            .tint(.primary)
                        }
                    }
                } header: {
                    HStack {
                        Text("Devices")
                        Spacer()
                        Button {
                            showingBLEPairing = true
                        } label: {
                            Image(systemName: "dot.radiowaves.left.and.right")
                        }
                        Button {
                            state.refreshInputs()
                        } label: {
                            Image(systemName: "arrow.clockwise")
                        }
                    }
                }
            }
            .navigationTitle("elepiano")
            .sheet(isPresented: $showingBLEPairing, onDismiss: {
                state.refreshInputs()
            }) {
                NavigationStack {
                    BLEMIDIView()
                        .navigationTitle("BLE-MIDI")
                        .navigationBarTitleDisplayMode(.inline)
                        .toolbar {
                            ToolbarItem(placement: .confirmationAction) {
                                Button("Done") { showingBLEPairing = false }
                            }
                        }
                }
            }
        }
    }
}
