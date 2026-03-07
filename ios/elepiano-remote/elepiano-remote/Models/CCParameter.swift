import SwiftUI

enum CCControlType {
    case fader
    case toggle(off: String, on: String)
    case selector(choices: [(value: Int, label: String)])
    case drawbar
}

struct CCParameter: Identifiable {
    let id: Int  // CC number
    let name: String
    let defaultValue: Int
    let group: CCGroup
    let controlType: CCControlType

    var cc: Int { id }

    var isToggle: Bool {
        if case .toggle = controlType { return true }
        return false
    }

    var toggleLabels: (off: String, on: String)? {
        if case .toggle(let off, let on) = controlType {
            return (off: off, on: on)
        }
        return nil
    }

    init(id: Int, name: String, defaultValue: Int, group: CCGroup,
         controlType: CCControlType = .fader) {
        self.id = id
        self.name = name
        self.defaultValue = defaultValue
        self.group = group
        self.controlType = controlType
    }

    // Convenience for toggle
    init(id: Int, name: String, defaultValue: Int, group: CCGroup,
         isToggle: Bool, toggleLabels: (off: String, on: String)? = nil) {
        self.id = id
        self.name = name
        self.defaultValue = defaultValue
        self.group = group
        if isToggle {
            let labels = toggleLabels ?? (off: "Off", on: "On")
            self.controlType = .toggle(off: labels.off, on: labels.on)
        } else {
            self.controlType = .fader
        }
    }
}

enum CCGroup: String, CaseIterable {
    case volume = "Volume"
    case modulation = "Modulation"
    case overdrive = "Overdrive"
    case release = "Release"
    case ir = "IR"
    case eq = "EQ"
    case space = "Space"
    case chorus = "Chorus"
    case drawbar = "Drawbar"
    case percussion = "Percussion"
    case vibrato = "Vibrato"
    case organChar = "OrganChar"
}

enum CCPage: String, CaseIterable {
    case mix = "Mix"
    case tone = "Tone"
    case space = "Space"
    case organ = "Organ"

    var color: Color {
        switch self {
        case .mix: return .blue
        case .tone: return .orange
        case .space: return .purple
        case .organ: return .red
        }
    }

    var groups: [CCGroup] {
        switch self {
        case .mix: return [.volume, .overdrive, .modulation, .release]
        case .tone: return [.ir, .eq]
        case .space: return [.space, .chorus]
        case .organ: return [.drawbar, .percussion, .vibrato, .organChar]
        }
    }

    var parameters: [CCParameter] {
        let groupSet = Set(groups)
        return CCParameter.all.filter { groupSet.contains($0.group) }
    }

    func faderColor(for group: CCGroup) -> Color {
        switch group {
        case .volume: return .blue
        case .modulation: return .green
        case .release: return .blue
        case .overdrive: return .orange
        case .ir, .eq: return .orange
        case .space, .chorus: return .purple
        case .drawbar, .percussion, .vibrato, .organChar: return .red
        }
    }
}

// MARK: - Selector Choices

enum SelectorChoices {
    static let irMic: [(value: Int, label: String)] = [
        (0, "Neve Warm"), (8, "Neve Bright"), (16, "API Punch"),
        (24, "SSL Clean"), (32, "Tape Warm"), (40, "Direct"),
        (48, "Dome L19"), (56, "Dome e609"), (64, "Center L19"), (72, "Center e609"),
        (80, "Cone20 L19"), (88, "Cone20 e609"), (96, "Cone L19"), (104, "Cone e609"),
        (112, "Cab+Back"), (120, "Dome+Back")
    ]

    static let spaceMode: [(value: Int, label: String)] = [
        (0, "OFF"), (32, "Tape"), (64, "Room"), (96, "Plate")
    ]

    static let vibratoKnob: [(value: Int, label: String)] = [
        (0, "OFF"), (18, "V1"), (36, "C1"), (54, "V2"), (73, "C2"), (91, "V3"), (109, "C3")
    ]
}

extension CCParameter {
    static let all: [CCParameter] = [
        // Mix page
        .init(id: 102, name: "Volume", defaultValue: 127, group: .volume),
        .init(id: 106, name: "Drive", defaultValue: 0, group: .overdrive),
        .init(id: 107, name: "Tone", defaultValue: 64, group: .overdrive),
        .init(id: 3, name: "T/P", defaultValue: 0, group: .modulation,
              isToggle: true, toggleLabels: (off: "Trem", on: "Phase")),
        .init(id: 1, name: "Depth", defaultValue: 0, group: .modulation),
        .init(id: 2, name: "Rate", defaultValue: 64, group: .modulation),
        .init(id: 4, name: "Color", defaultValue: 71, group: .modulation),
        .init(id: 5, name: "Release", defaultValue: 10, group: .release),

        // Tone page
        .init(id: 104, name: "IR Mic", defaultValue: 0, group: .ir,
              controlType: .selector(choices: SelectorChoices.irMic)),
        .init(id: 105, name: "IR Wet", defaultValue: 127, group: .ir),
        .init(id: 108, name: "Lo EQ", defaultValue: 64, group: .eq),
        .init(id: 109, name: "Hi EQ", defaultValue: 64, group: .eq),

        // Space page
        .init(id: 110, name: "Mode", defaultValue: 0, group: .space,
              controlType: .selector(choices: SelectorChoices.spaceMode)),
        .init(id: 111, name: "Time", defaultValue: 64, group: .space),
        .init(id: 112, name: "Decay", defaultValue: 64, group: .space),
        .init(id: 113, name: "Wet", defaultValue: 0, group: .space),
        .init(id: 114, name: "Ch.Rate", defaultValue: 64, group: .chorus),
        .init(id: 115, name: "Ch.Dep", defaultValue: 64, group: .chorus),
        .init(id: 116, name: "Ch.Wet", defaultValue: 0, group: .chorus),

        // Organ page - Drawbars
        .init(id: 70, name: "16'", defaultValue: 127, group: .drawbar, controlType: .drawbar),
        .init(id: 71, name: "5⅓'", defaultValue: 0, group: .drawbar, controlType: .drawbar),
        .init(id: 72, name: "8'", defaultValue: 127, group: .drawbar, controlType: .drawbar),
        .init(id: 73, name: "4'", defaultValue: 0, group: .drawbar, controlType: .drawbar),
        .init(id: 74, name: "2⅔'", defaultValue: 0, group: .drawbar, controlType: .drawbar),
        .init(id: 75, name: "2'", defaultValue: 0, group: .drawbar, controlType: .drawbar),
        .init(id: 76, name: "1⅗'", defaultValue: 0, group: .drawbar, controlType: .drawbar),
        .init(id: 77, name: "1⅓'", defaultValue: 0, group: .drawbar, controlType: .drawbar),
        .init(id: 78, name: "1'", defaultValue: 0, group: .drawbar, controlType: .drawbar),

        // Organ page - Vibrato/Chorus
        .init(id: 92, name: "Vib", defaultValue: 0, group: .vibrato,
              controlType: .selector(choices: SelectorChoices.vibratoKnob)),
        .init(id: 31, name: "Upper", defaultValue: 0, group: .vibrato,
              isToggle: true, toggleLabels: (off: "Off", on: "On")),

        // Organ page - Percussion toggles
        .init(id: 80, name: "Perc", defaultValue: 0, group: .percussion,
              isToggle: true, toggleLabels: (off: "Off", on: "On")),
        .init(id: 81, name: "Perc Vol", defaultValue: 0, group: .percussion,
              isToggle: true, toggleLabels: (off: "Soft", on: "Normal")),
        .init(id: 82, name: "Perc Decay", defaultValue: 0, group: .percussion,
              isToggle: true, toggleLabels: (off: "Slow", on: "Fast")),
        .init(id: 83, name: "Perc Harm", defaultValue: 0, group: .percussion,
              isToggle: true, toggleLabels: (off: "2nd", on: "3rd")),

        // Organ page - Character
        .init(id: 21, name: "Click", defaultValue: 96, group: .organChar),
        .init(id: 22, name: "Leak", defaultValue: 0, group: .organChar),
        .init(id: 23, name: "Aging", defaultValue: 0, group: .organChar),
    ]

    static func grouped() -> [(group: CCGroup, params: [CCParameter])] {
        CCGroup.allCases.compactMap { group in
            let params = all.filter { $0.group == group }
            return params.isEmpty ? nil : (group: group, params: params)
        }
    }
}
