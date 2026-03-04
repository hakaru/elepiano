import Foundation
import MIDIKitIO

@Observable
final class AppState {
    let midiManager = MIDIManager(
        clientName: "ElepianoControl",
        model: "ElepianoControl",
        manufacturer: "hakaru"
    )

    // MIDI 接続状態
    private(set) var isConnected = false
    private(set) var connectedDeviceName: String?
    private(set) var availableInputs: [MIDIInputEndpoint] = []

    // 現在のモード
    var mode: EngineMode = .piano

    // FX パラメータ (CC 0-127 値)
    var fxValues: [Int: Int] = [
        70: 0,   // Drive
        71: 64,  // Lo EQ (center)
        72: 64,  // Hi EQ (center)
        73: 0,   // Tremolo Depth
        74: 64,  // Tremolo Rate
        75: 0,   // Delay Time
        76: 64,  // Delay Feedback
        77: 64,  // Chorus Rate
        78: 64,  // Chorus Depth
        79: 0,   // Chorus Wet
    ]

    // オルガン Drawbar 値 (0-8)
    // [section][drawbar_idx]
    var drawbars: [[Int]] = [
        [0, 0, 8, 8, 6, 0, 0, 0, 0],  // Upper
        [0, 0, 8, 8, 0, 0, 0, 0, 0],  // Lower
        [8, 0, 8, 0, 0, 0, 0, 0, 0],  // Pedal
    ]

    // Leslie 状態
    var leslieFast = false

    // プリセット
    var presets: [Preset] = []
    private let presetStore = PresetStore()

    private static let connectionTag = "ElepianoOutput"

    func start() {
        do {
            try midiManager.start()
        } catch {
            print("[MIDI] start failed: \(error)")
        }
        refreshInputs()
        presets = presetStore.loadAll()
    }

    func refreshInputs() {
        availableInputs = midiManager.endpoints.inputs
    }

    func connect(to endpoint: MIDIInputEndpoint) {
        // 既存接続を切断
        disconnect()

        do {
            try midiManager.addOutputConnection(
                to: .inputs([endpoint]),
                tag: Self.connectionTag
            )
            isConnected = true
            connectedDeviceName = endpoint.displayName
        } catch {
            print("[MIDI] connect failed: \(error)")
        }
    }

    func disconnect() {
        midiManager.remove(.outputConnection, .withTag(Self.connectionTag))
        isConnected = false
        connectedDeviceName = nil
    }

    // MARK: - MIDI 送信

    func sendCC(channel: UInt4 = 0, cc: UInt7, value: UInt7) {
        guard let conn = midiManager.managedOutputConnections[Self.connectionTag] else { return }
        try? conn.send(event: .cc(cc, value: .midi1(value), channel: channel))
    }

    func sendPC(channel: UInt4 = 0, program: UInt7) {
        guard let conn = midiManager.managedOutputConnections[Self.connectionTag] else { return }
        try? conn.send(event: .programChange(program: program, channel: channel))
    }

    // MARK: - FX パラメータ送信

    func updateFX(cc: Int, value: Int) {
        let clamped = max(0, min(127, value))
        fxValues[cc] = clamped
        sendCC(cc: UInt7(cc), value: UInt7(clamped))
    }

    // MARK: - Drawbar 送信

    func updateDrawbar(section: Int, index: Int, value: Int) {
        guard section < 3, index < 9 else { return }
        let clamped = max(0, min(8, value))
        drawbars[section][index] = clamped

        let channel = UInt4(section)
        let ccNum = UInt7(12 + index)
        // drawbar 0-8 → CC 0-127
        let ccVal = UInt7(clamped * 127 / 8)
        sendCC(channel: channel, cc: ccNum, value: ccVal)
    }

    func toggleLeslie() {
        leslieFast.toggle()
        sendCC(cc: 64, value: leslieFast ? 127 : 0)
    }

    // MARK: - プリセット

    func savePreset(name: String) {
        let preset = Preset(
            name: name,
            mode: mode,
            fxValues: fxValues,
            drawbars: drawbars,
            leslieFast: leslieFast
        )
        presets.append(preset)
        presetStore.saveAll(presets)
    }

    func loadPreset(_ preset: Preset) {
        mode = preset.mode
        fxValues = preset.fxValues
        drawbars = preset.drawbars
        leslieFast = preset.leslieFast

        // 全パラメータを送信
        for (cc, val) in fxValues {
            sendCC(cc: UInt7(cc), value: UInt7(val))
        }
        for section in 0..<3 {
            for idx in 0..<9 {
                updateDrawbar(section: section, index: idx, value: drawbars[section][idx])
            }
        }
        sendCC(cc: 64, value: leslieFast ? 127 : 0)
    }

    func deletePreset(at index: Int) {
        guard index < presets.count else { return }
        presets.remove(at: index)
        presetStore.saveAll(presets)
    }
}

enum EngineMode: String, Codable, CaseIterable {
    case piano = "Piano"
    case organ = "Organ"
}
