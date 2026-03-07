import SwiftUI

struct CCControlView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var ccValues: [Int: Double] = [:]

    private let groups = CCParameter.grouped()

    var body: some View {
        VStack(spacing: 0) {
            StatusView()

            ScrollView {
                LazyVStack(spacing: 16) {
                    // Program Change
                    programSection

                    // CC Groups
                    ForEach(groups, id: \.group) { group in
                        ccGroupSection(group.group, params: group.params)
                    }
                }
                .padding()
            }
        }
        .navigationTitle("elepiano")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    ble.disconnect()
                } label: {
                    Image(systemName: "wifi.slash")
                }
            }
        }
        .onAppear {
            // デフォルト値で初期化
            for param in CCParameter.all {
                if ccValues[param.cc] == nil {
                    ccValues[param.cc] = Double(param.defaultValue)
                }
            }
        }
    }

    // MARK: - Program Change

    private var programSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Program")
                .font(.headline)
                .foregroundStyle(.secondary)

            HStack(spacing: 12) {
                ForEach(Array(["Rhodes", "LA Custom", "Vintage Vibe"].enumerated()), id: \.offset) { index, name in
                    let isActive = (ble.status?.program ?? 1) == (index + 1)
                    Button {
                        ble.sendProgramChange(index)
                    } label: {
                        Text(name)
                            .font(.subheadline)
                            .fontWeight(isActive ? .bold : .regular)
                            .padding(.horizontal, 16)
                            .padding(.vertical, 8)
                            .background(isActive ? Color.blue : Color.secondary.opacity(0.15))
                            .foregroundColor(isActive ? .white : .primary)
                            .cornerRadius(8)
                    }
                }
                Spacer()
            }
        }
        .padding()
        .background(.ultraThinMaterial)
        .cornerRadius(12)
    }

    // MARK: - CC Group

    private func ccGroupSection(_ group: CCGroup, params: [CCParameter]) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(group.rawValue)
                .font(.headline)
                .foregroundStyle(.secondary)

            ForEach(params) { param in
                if param.cc == 3 {
                    ccToggle(param, offLabel: "Tremolo", onLabel: "Phaser")
                } else {
                    ccSlider(param)
                }
            }
        }
        .padding()
        .background(.ultraThinMaterial)
        .cornerRadius(12)
    }

    private func ccToggle(_ param: CCParameter, offLabel: String, onLabel: String) -> some View {
        let isOn = (ccValues[param.cc] ?? Double(param.defaultValue)) >= 64
        return HStack {
            Text(offLabel)
                .font(.subheadline)
                .foregroundStyle(isOn ? .secondary : .primary)
            Toggle("", isOn: Binding(
                get: { isOn },
                set: { newValue in
                    let val = newValue ? 127.0 : 0.0
                    ccValues[param.cc] = val
                    ble.sendCC(param.cc, value: Int(val))
                }
            ))
            .labelsHidden()
            Text(onLabel)
                .font(.subheadline)
                .foregroundStyle(isOn ? .primary : .secondary)
        }
    }

    private func ccSlider(_ param: CCParameter) -> some View {
        VStack(spacing: 4) {
            HStack {
                Text(param.name)
                    .font(.subheadline)
                Spacer()
                Text("\(Int(ccValues[param.cc] ?? Double(param.defaultValue)))")
                    .font(.caption)
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }

            Slider(
                value: Binding(
                    get: { ccValues[param.cc] ?? Double(param.defaultValue) },
                    set: { newValue in
                        ccValues[param.cc] = newValue
                        ble.sendCC(param.cc, value: Int(newValue))
                    }
                ),
                in: 0...127,
                step: 1
            )
        }
    }
}
