import SwiftUI

struct OrganControlView: View {
    @Environment(AppState.self) private var state

    private let drawbarLabels = ["16'", "5⅓'", "8'", "4'", "2⅔'", "2'", "1⅗'", "1⅓'", "1'"]
    private let sectionNames = ["Upper", "Lower", "Pedal"]

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 20) {
                    // Leslie トグル
                    GroupBox {
                        HStack {
                            Text("Leslie")
                                .font(.headline)
                            Spacer()
                            Button {
                                state.toggleLeslie()
                            } label: {
                                Text(state.leslieFast ? "FAST" : "SLOW")
                                    .font(.headline.bold())
                                    .foregroundStyle(.white)
                                    .padding(.horizontal, 20)
                                    .padding(.vertical, 8)
                                    .background(state.leslieFast ? .red : .green,
                                                in: Capsule())
                            }
                        }
                    }

                    // 3セクションのドローバー
                    ForEach(0..<3, id: \.self) { section in
                        GroupBox(sectionNames[section]) {
                            DrawbarRow(
                                section: section,
                                labels: drawbarLabels,
                                values: state.drawbars[section]
                            )
                        }
                    }
                }
                .padding()
            }
            .navigationTitle("Organ")
        }
    }
}

struct DrawbarRow: View {
    let section: Int
    let labels: [String]
    let values: [Int]
    @Environment(AppState.self) private var state

    var body: some View {
        HStack(spacing: 6) {
            ForEach(0..<9, id: \.self) { idx in
                VStack(spacing: 4) {
                    Text("\(values[idx])")
                        .font(.caption2.monospacedDigit().bold())
                    // 縦スライダー
                    VerticalSlider(
                        value: Binding(
                            get: { Double(values[idx]) },
                            set: { state.updateDrawbar(section: section, index: idx, value: Int($0)) }
                        ),
                        range: 0...8
                    )
                    .frame(height: 120)
                    Text(labels[idx])
                        .font(.system(size: 9))
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(.vertical, 4)
    }
}

struct VerticalSlider: View {
    @Binding var value: Double
    let range: ClosedRange<Double>

    var body: some View {
        GeometryReader { geo in
            let height = geo.size.height
            let step = (range.upperBound - range.lowerBound)
            let normalized = (value - range.lowerBound) / step
            let yPos = height * (1.0 - normalized)

            ZStack(alignment: .bottom) {
                // Track
                Capsule()
                    .fill(.quaternary)
                    .frame(width: 8)
                // Fill
                Capsule()
                    .fill(.blue)
                    .frame(width: 8, height: height - yPos)
            }
            .frame(maxWidth: .infinity)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        let ratio = 1.0 - (drag.location.y / height)
                        let clamped = min(max(ratio, 0), 1)
                        value = (range.lowerBound + clamped * step).rounded()
                    }
            )
        }
    }
}
