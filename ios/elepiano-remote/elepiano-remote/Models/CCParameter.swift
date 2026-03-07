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
    case modulation = "Modulation"
    case ir = "IR"
    case overdrive = "Overdrive"
    case eq = "EQ"
    case space = "Space"
    case chorus = "Chorus"
    case release = "Release"
    case organ = "Organ"
}

extension CCParameter {
    static let all: [CCParameter] = [
        // Volume
        .init(id: 102, name: "Volume", defaultValue: 127, group: .volume),
        .init(id: 103, name: "Expression", defaultValue: 127, group: .volume),
        // Modulation (Tremolo / Phaser 共有)
        .init(id: 3, name: "Mode", defaultValue: 0, group: .modulation),  // 0=Tremolo, 127=Phaser
        .init(id: 1, name: "Depth", defaultValue: 0, group: .modulation),
        .init(id: 2, name: "Rate", defaultValue: 64, group: .modulation),
        .init(id: 4, name: "Color", defaultValue: 71, group: .modulation),  // Phaser feedback
        // IR Convolver
        .init(id: 104, name: "Mic Position", defaultValue: 0, group: .ir),
        .init(id: 105, name: "Wet", defaultValue: 0, group: .ir),
        // Overdrive
        .init(id: 106, name: "Drive", defaultValue: 0, group: .overdrive),
        .init(id: 107, name: "Tone", defaultValue: 64, group: .overdrive),
        // EQ
        .init(id: 108, name: "Lo", defaultValue: 64, group: .eq),
        .init(id: 109, name: "Hi", defaultValue: 64, group: .eq),
        // Space
        .init(id: 110, name: "Mode", defaultValue: 0, group: .space),
        .init(id: 111, name: "Time/Size", defaultValue: 64, group: .space),
        .init(id: 112, name: "Decay/FB", defaultValue: 64, group: .space),
        .init(id: 113, name: "Wet", defaultValue: 0, group: .space),
        // Chorus
        .init(id: 114, name: "Rate", defaultValue: 64, group: .chorus),
        .init(id: 115, name: "Depth", defaultValue: 64, group: .chorus),
        .init(id: 116, name: "Wet", defaultValue: 0, group: .chorus),
        // Release
        .init(id: 5, name: "Release Time", defaultValue: 10, group: .release),
        // Organ (vintage character)
        .init(id: 21, name: "Key Click", defaultValue: 96, group: .organ),
        .init(id: 22, name: "Leakage", defaultValue: 0, group: .organ),
        .init(id: 23, name: "Aging", defaultValue: 0, group: .organ),
    ]

    static func grouped() -> [(group: CCGroup, params: [CCParameter])] {
        CCGroup.allCases.compactMap { group in
            let params = all.filter { $0.group == group }
            return params.isEmpty ? nil : (group: group, params: params)
        }
    }
}
