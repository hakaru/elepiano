import SwiftUI

struct PianoFXView: View {
    @Environment(AppState.self) private var state

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 20) {
                    // Drive & EQ
                    GroupBox("Drive / EQ") {
                        VStack(spacing: 12) {
                            CCSlider("Drive", cc: 70, range: 1...8, format: "%.1f")
                            CCSlider("Lo EQ", cc: 71, range: -12...12, unit: "dB", format: "%+.0f")
                            CCSlider("Hi EQ", cc: 72, range: -12...12, unit: "dB", format: "%+.0f")
                        }
                        .padding(.vertical, 4)
                    }

                    // Tremolo
                    GroupBox("Tremolo") {
                        VStack(spacing: 12) {
                            CCSlider("Depth", cc: 73, range: 0...0.8, format: "%.2f")
                            CCSlider("Rate", cc: 74, range: 0.5...8, unit: "Hz", format: "%.1f")
                        }
                        .padding(.vertical, 4)
                    }

                    // Chorus
                    GroupBox("Chorus") {
                        VStack(spacing: 12) {
                            CCSlider("Rate", cc: 77, range: 0.5...2, unit: "Hz", format: "%.1f")
                            CCSlider("Depth", cc: 78, range: 0...20, unit: "ms", format: "%.0f")
                            CCSlider("Wet", cc: 79, range: 0...1, format: "%.2f")
                        }
                        .padding(.vertical, 4)
                    }

                    // Tape Delay
                    GroupBox("Tape Delay") {
                        VStack(spacing: 12) {
                            CCSlider("Time", cc: 75, range: 0...0.5, unit: "s", format: "%.2f")
                            CCSlider("Feedback", cc: 76, range: 0...0.85, format: "%.2f")
                        }
                        .padding(.vertical, 4)
                    }
                }
                .padding()
            }
            .navigationTitle("Piano FX")
        }
    }
}
