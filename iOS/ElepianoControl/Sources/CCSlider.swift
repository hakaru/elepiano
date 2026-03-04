import SwiftUI

struct CCSlider: View {
    let label: String
    let cc: Int
    let range: ClosedRange<Double>
    let unit: String
    let format: String

    @Environment(AppState.self) private var state

    init(_ label: String, cc: Int,
         range: ClosedRange<Double> = 0...127,
         unit: String = "",
         format: String = "%.0f") {
        self.label = label
        self.cc = cc
        self.range = range
        self.unit = unit
        self.format = format
    }

    private var ccValue: Double {
        Double(state.fxValues[cc] ?? 0)
    }

    /// CC 0-127 を物理単位に変換
    private var displayValue: Double {
        let normalized = ccValue / 127.0
        return range.lowerBound + normalized * (range.upperBound - range.lowerBound)
    }

    var body: some View {
        VStack(spacing: 4) {
            HStack {
                Text(label)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Text("\(String(format: format, displayValue))\(unit)")
                    .font(.caption.monospacedDigit())
            }
            Slider(value: Binding(
                get: { ccValue },
                set: { state.updateFX(cc: cc, value: Int($0)) }
            ), in: 0...127, step: 1)
        }
    }
}
