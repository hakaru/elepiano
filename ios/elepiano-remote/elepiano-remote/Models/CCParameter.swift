import Foundation

struct CCParameter: Identifiable {
    let id: Int  // CC number
    let name: String
    let defaultValue: Int
    let group: CCGroup

    var cc: Int { id }
}

enum CCGroup: String, CaseIterable {
    case volume = "Volume"
    case tremolo = "Tremolo"
    case overdrive = "Overdrive"
    case eq = "EQ"
    case release = "Release"
    case space = "Space"
    case chorus = "Chorus"
    case pedalResonance = "Pedal Resonance"
    case pedalClick = "Pedal Click"
}

extension CCParameter {
    static let all: [CCParameter] = [
        // Volume
        .init(id: 7, name: "Volume", defaultValue: 127, group: .volume),
        .init(id: 11, name: "Expression", defaultValue: 127, group: .volume),
        // Tremolo
        .init(id: 1, name: "Depth", defaultValue: 0, group: .tremolo),
        .init(id: 2, name: "Rate", defaultValue: 64, group: .tremolo),
        // Overdrive
        .init(id: 70, name: "Drive", defaultValue: 0, group: .overdrive),
        .init(id: 71, name: "Tone", defaultValue: 64, group: .overdrive),
        // EQ
        .init(id: 72, name: "Lo", defaultValue: 64, group: .eq),
        .init(id: 73, name: "Hi", defaultValue: 64, group: .eq),
        // Release
        .init(id: 74, name: "Release Time", defaultValue: 32, group: .release),
        // Space
        .init(id: 75, name: "Mode", defaultValue: 0, group: .space),
        .init(id: 76, name: "Time/Size", defaultValue: 64, group: .space),
        .init(id: 77, name: "Decay/FB", defaultValue: 64, group: .space),
        .init(id: 78, name: "Wet", defaultValue: 0, group: .space),
        // Chorus
        .init(id: 79, name: "Rate", defaultValue: 64, group: .chorus),
        .init(id: 80, name: "Depth", defaultValue: 64, group: .chorus),
        .init(id: 81, name: "Wet", defaultValue: 0, group: .chorus),
        // Pedal Resonance
        .init(id: 82, name: "Volume", defaultValue: 48, group: .pedalResonance),
        .init(id: 83, name: "Attack", defaultValue: 51, group: .pedalResonance),
        .init(id: 84, name: "Release", defaultValue: 55, group: .pedalResonance),
        // Pedal Click
        .init(id: 85, name: "Volume", defaultValue: 48, group: .pedalClick),
        .init(id: 86, name: "Attack", defaultValue: 9, group: .pedalClick),
        .init(id: 87, name: "Release", defaultValue: 27, group: .pedalClick),
    ]

    static func grouped() -> [(group: CCGroup, params: [CCParameter])] {
        CCGroup.allCases.compactMap { group in
            let params = all.filter { $0.group == group }
            return params.isEmpty ? nil : (group: group, params: params)
        }
    }
}
