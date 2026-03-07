import SwiftUI

struct FaderView: View {
    let param: CCParameter
    let color: Color
    @Binding var value: Double
    var onChanged: ((Int) -> Void)?

    private let trackWidth: CGFloat = 56
    private let thumbHeight: CGFloat = 24

    var body: some View {
        VStack(spacing: 4) {
            Text("\(Int(value))")
                .font(.system(size: 11, weight: .medium, design: .monospaced))
                .foregroundStyle(.secondary)

            GeometryReader { geo in
                let trackH = geo.size.height
                let usable = trackH - thumbHeight
                let fillH = usable * value / 127.0

                ZStack(alignment: .bottom) {
                    RoundedRectangle(cornerRadius: 6)
                        .fill(Color(.systemGray5))

                    RoundedRectangle(cornerRadius: 6)
                        .fill(color.opacity(0.5))
                        .frame(height: fillH + thumbHeight / 2)

                    RoundedRectangle(cornerRadius: 4)
                        .fill(.white)
                        .shadow(color: .black.opacity(0.3), radius: 2, y: 1)
                        .frame(width: trackWidth + 8, height: thumbHeight)
                        .offset(y: -fillH)
                }
                .frame(width: trackWidth)
                .frame(maxWidth: .infinity)
                .contentShape(Rectangle())
                .highPriorityGesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { drag in
                            let y = drag.location.y
                            let clamped = min(max(y, thumbHeight / 2), trackH - thumbHeight / 2)
                            let normalized = 1.0 - (clamped - thumbHeight / 2) / usable
                            let newVal = normalized * 127.0
                            value = min(max(newVal, 0), 127)
                            onChanged?(Int(value))
                        }
                )
            }

            Text(param.name)
                .font(.system(size: 10, weight: .medium))
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity)
    }
}

struct ToggleFaderView: View {
    let param: CCParameter
    let color: Color
    @Binding var value: Double
    var onChanged: ((Int) -> Void)?

    var body: some View {
        let isOn = value >= 64
        let labels = param.toggleLabels ?? (off: "Off", on: "On")

        VStack(spacing: 8) {
            Text(isOn ? labels.on : labels.off)
                .font(.system(size: 11, weight: .bold))
                .foregroundStyle(color)

            Spacer()

            Button {
                let newVal = isOn ? 0.0 : 127.0
                value = newVal
                onChanged?(Int(newVal))
            } label: {
                RoundedRectangle(cornerRadius: 8)
                    .fill(isOn ? color : Color(.systemGray5))
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .strokeBorder(color.opacity(0.5), lineWidth: 1)
                    )
                    .frame(width: 48, height: 48)
                    .overlay(
                        Text(isOn ? "ON" : "OFF")
                            .font(.system(size: 12, weight: .bold))
                            .foregroundColor(isOn ? .white : .secondary)
                    )
            }

            Spacer()

            Text(param.name)
                .font(.system(size: 10, weight: .medium))
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - SelectorView

struct SelectorView: View {
    let param: CCParameter
    let color: Color
    let choices: [(value: Int, label: String)]
    @Binding var value: Double
    var onChanged: ((Int) -> Void)?

    private var currentIndex: Int {
        let intVal = Int(value)
        // Find closest match
        var best = 0
        var bestDist = Int.max
        for (i, choice) in choices.enumerated() {
            let dist = abs(choice.value - intVal)
            if dist < bestDist {
                bestDist = dist
                best = i
            }
        }
        return best
    }

    var body: some View {
        VStack(spacing: 4) {
            // Current selection label
            Text(choices[currentIndex].label)
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundStyle(color)
                .lineLimit(1)
                .minimumScaleFactor(0.6)

            Spacer()

            // Tap to cycle
            Button {
                let next = (currentIndex + 1) % choices.count
                let newVal = Double(choices[next].value)
                value = newVal
                onChanged?(choices[next].value)
            } label: {
                RoundedRectangle(cornerRadius: 8)
                    .fill(color.opacity(0.15))
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .strokeBorder(color.opacity(0.5), lineWidth: 1)
                    )
                    .frame(maxWidth: .infinity)
                    .frame(height: 56)
                    .overlay(
                        VStack(spacing: 2) {
                            Image(systemName: "chevron.up.chevron.down")
                                .font(.system(size: 14))
                                .foregroundStyle(color)
                            Text("\(currentIndex + 1)/\(choices.count)")
                                .font(.system(size: 10, design: .monospaced))
                                .foregroundStyle(.secondary)
                        }
                    )
            }

            Spacer()

            Text(param.name)
                .font(.system(size: 10, weight: .medium))
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - DrawbarView (inverted fader: top=0, bottom=127)

struct DrawbarView: View {
    let param: CCParameter
    let color: Color
    @Binding var value: Double
    var onChanged: ((Int) -> Void)?

    private let trackWidth: CGFloat = 40
    private let thumbHeight: CGFloat = 20

    // Drawbar colors (Hammond style)
    private var drawbarColor: Color {
        switch param.name {
        case "16'": return .brown
        case "5⅓'": return .brown
        case "8'": return .white
        case "4'": return .white
        case "2⅔'": return .black
        case "2'": return .white
        case "1⅗'": return .black
        case "1⅓'": return .black
        case "1'": return .white
        default: return .white
        }
    }

    private var drawbarTextColor: Color {
        drawbarColor == .black ? .white : .black
    }

    var body: some View {
        VStack(spacing: 2) {
            // Inverted: show value at top
            Text("\(Int(value / 127.0 * 8))")
                .font(.system(size: 10, weight: .medium, design: .monospaced))
                .foregroundStyle(.secondary)

            GeometryReader { geo in
                let trackH = geo.size.height
                let usable = trackH - thumbHeight
                // Inverted: value=0 → thumb at top, value=127 → thumb at bottom
                let thumbY = usable * value / 127.0

                ZStack(alignment: .top) {
                    // Background track
                    RoundedRectangle(cornerRadius: 4)
                        .fill(Color(.systemGray5))

                    // Fill from top
                    RoundedRectangle(cornerRadius: 4)
                        .fill(color.opacity(0.3))
                        .frame(height: thumbY + thumbHeight / 2)

                    // Thumb
                    RoundedRectangle(cornerRadius: 3)
                        .fill(drawbarColor)
                        .shadow(color: .black.opacity(0.3), radius: 2, y: 1)
                        .frame(width: trackWidth + 4, height: thumbHeight)
                        .overlay(
                            RoundedRectangle(cornerRadius: 3)
                                .strokeBorder(Color.gray.opacity(0.5), lineWidth: 0.5)
                        )
                        .offset(y: thumbY)
                }
                .frame(width: trackWidth)
                .frame(maxWidth: .infinity)
                .contentShape(Rectangle())
                .highPriorityGesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { drag in
                            let y = drag.location.y
                            let clamped = min(max(y, thumbHeight / 2), trackH - thumbHeight / 2)
                            // Inverted: top=0, bottom=127
                            let normalized = (clamped - thumbHeight / 2) / usable
                            let newVal = normalized * 127.0
                            value = min(max(newVal, 0), 127)
                            onChanged?(Int(value))
                        }
                )
            }

            // Drawbar label
            Text(param.name)
                .font(.system(size: 9, weight: .bold))
                .lineLimit(1)
                .minimumScaleFactor(0.6)
        }
        .frame(maxWidth: .infinity)
    }
}
