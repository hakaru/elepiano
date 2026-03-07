import SwiftUI

struct LandscapeCCView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var ccValues: [Int: Double] = [:]
    @State private var selectedPage: CCPage = .mix
    @State private var selectedProgram: Int = 0  // 0-based, local tracking

    private let programs = ["R", "LA", "VV", "Org"]

    var body: some View {
        HStack(spacing: 0) {
            sidebar

            VStack(spacing: 0) {
                topBar

                pageContent(selectedPage)
                    .padding(.horizontal, 8)
                    .padding(.bottom, 4)
            }
        }
        .background(Color(.systemBackground))
        .onAppear {
            for param in CCParameter.all {
                if ccValues[param.cc] == nil {
                    ccValues[param.cc] = Double(param.defaultValue)
                }
            }
        }
        .onChange(of: ble.status?.program) { newPG in
            if let pg = newPG {
                selectedProgram = pg - 1  // 1-based → 0-based
            }
        }
    }

    // MARK: - Sidebar

    private var sidebar: some View {
        VStack(spacing: 0) {
            ForEach(Array(programs.enumerated()), id: \.offset) { index, label in
                let isActive = selectedProgram == index
                Button {
                    selectedProgram = index
                    ble.sendProgramChange(index)
                } label: {
                    Text(label)
                        .font(.system(size: 14, weight: isActive ? .bold : .medium))
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(isActive ? Color.accentColor : Color(.systemGray5))
                        .foregroundColor(isActive ? .white : .primary)
                }
            }
        }
        .frame(width: 52)
        .background(Color(.secondarySystemBackground))
    }

    // MARK: - Top Bar

    private var topBar: some View {
        HStack(spacing: 0) {
            HStack(spacing: 2) {
                ForEach(CCPage.allCases, id: \.self) { page in
                    Button {
                        withAnimation {
                            selectedPage = page
                        }
                    } label: {
                        Text(page.rawValue)
                            .font(.system(size: 12, weight: selectedPage == page ? .bold : .medium))
                            .padding(.horizontal, 12)
                            .padding(.vertical, 5)
                            .background(selectedPage == page ? page.color.opacity(0.2) : .clear)
                            .foregroundColor(selectedPage == page ? page.color : .secondary)
                            .cornerRadius(4)
                    }
                }
            }

            Spacer()

            CompactStatusView()

            if let status = ble.status, status.isReady {
                Text(status.name ?? "---")
                    .font(.system(size: 12, weight: .semibold))
                    .padding(.horizontal, 6)

                Text("\(status.voices ?? 0)v")
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(.secondary)
            }

            Menu {
                Button { ble.sendCommand("restart_elepiano") } label: {
                    Label("Restart elepiano", systemImage: "arrow.clockwise")
                }
                Button { ble.sendCommand("restart_ble_bridge") } label: {
                    Label("Restart BLE", systemImage: "antenna.radiowaves.left.and.right")
                }
                Button { ble.disconnect() } label: {
                    Label("Disconnect", systemImage: "wifi.slash")
                }
            } label: {
                Image(systemName: "ellipsis.circle")
                    .font(.system(size: 14))
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 8)
            }
        }
        .padding(.horizontal, 8)
        .frame(height: 32)
        .background(Color(.secondarySystemBackground).opacity(0.8))
    }

    // MARK: - Page Content

    @ViewBuilder
    private func pageContent(_ page: CCPage) -> some View {
        switch page {
        case .organ:
            organLayout
        default:
            standardLayout(page)
        }
    }

    // MARK: - Standard Layout (Mix / Tone / Space)

    private func standardLayout(_ page: CCPage) -> some View {
        let params = page.parameters

        return HStack(spacing: 4) {
            ForEach(params) { param in
                let faderColor = page.faderColor(for: param.group)

                switch param.controlType {
                case .toggle:
                    ToggleFaderView(
                        param: param,
                        color: faderColor,
                        value: binding(for: param.cc, default: param.defaultValue),
                        onChanged: { val in ble.sendCC(param.cc, value: val) }
                    )
                case .selector(let choices):
                    SelectorView(
                        param: param,
                        color: faderColor,
                        choices: choices,
                        value: binding(for: param.cc, default: param.defaultValue),
                        onChanged: { val in ble.sendCC(param.cc, value: val) }
                    )
                case .fader, .drawbar:
                    FaderView(
                        param: param,
                        color: faderColor,
                        value: binding(for: param.cc, default: param.defaultValue),
                        onChanged: { val in ble.sendCC(param.cc, value: val) }
                    )
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Organ Layout

    private var organLayout: some View {
        let drawbars = CCParameter.all.filter { $0.group == .drawbar }
        let percToggles = CCParameter.all.filter { $0.group == .percussion }
        let vibratoParams = CCParameter.all.filter { $0.group == .vibrato }
        let charParams = CCParameter.all.filter { $0.group == .organChar }
        let organColor = CCPage.organ.color

        return HStack(spacing: 0) {
            // Left: 9 drawbars
            HStack(spacing: 2) {
                ForEach(drawbars) { param in
                    DrawbarView(
                        param: param,
                        color: organColor,
                        value: binding(for: param.cc, default: param.defaultValue),
                        onChanged: { val in ble.sendCC(param.cc, value: val) }
                    )
                }
            }
            .frame(maxWidth: .infinity)

            Rectangle()
                .fill(Color(.separator))
                .frame(width: 1)
                .padding(.vertical, 8)

            // Right: percussion + vibrato + character
            VStack(spacing: 0) {
                // Percussion toggles row
                HStack(spacing: 2) {
                    ForEach(percToggles) { param in
                        ToggleFaderView(
                            param: param,
                            color: organColor,
                            value: binding(for: param.cc, default: param.defaultValue),
                            onChanged: { val in ble.sendCC(param.cc, value: val) }
                        )
                    }
                }
                .frame(maxHeight: .infinity)

                Divider()

                // Vibrato + Character row
                HStack(spacing: 4) {
                    // Vibrato selector + upper toggle
                    ForEach(vibratoParams) { param in
                        switch param.controlType {
                        case .selector(let choices):
                            SelectorView(
                                param: param,
                                color: .yellow,
                                choices: choices,
                                value: binding(for: param.cc, default: param.defaultValue),
                                onChanged: { val in ble.sendCC(param.cc, value: val) }
                            )
                        case .toggle:
                            ToggleFaderView(
                                param: param,
                                color: .yellow,
                                value: binding(for: param.cc, default: param.defaultValue),
                                onChanged: { val in ble.sendCC(param.cc, value: val) }
                            )
                        default:
                            FaderView(
                                param: param,
                                color: .yellow,
                                value: binding(for: param.cc, default: param.defaultValue),
                                onChanged: { val in ble.sendCC(param.cc, value: val) }
                            )
                        }
                    }

                    // Character faders
                    ForEach(charParams) { param in
                        FaderView(
                            param: param,
                            color: organColor,
                            value: binding(for: param.cc, default: param.defaultValue),
                            onChanged: { val in ble.sendCC(param.cc, value: val) }
                        )
                    }
                }
                .frame(maxHeight: .infinity)
            }
            .frame(maxWidth: .infinity)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Helpers

    private func binding(for cc: Int, default defaultValue: Int) -> Binding<Double> {
        Binding(
            get: { ccValues[cc] ?? Double(defaultValue) },
            set: { ccValues[cc] = $0 }
        )
    }
}
