import Foundation
import MIDIKitIO

@MainActor @Observable
final class AppState {
    let midiManager = MIDIManager(
        clientName: "ElepianoControl",
        model: "ElepianoControl",
        manufacturer: "hakaru"
    )

    // MIDI 接続状態
    private(set) var isConnected = false
    private(set) var connectedEndpointID: MIDIIdentifier?
    private(set) var connectedDeviceName: String?
    private(set) var availableInputs: [MIDIInputEndpoint] = []
    private var started = false

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
        guard !started else { return }
        started = true
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
            connectedEndpointID = endpoint.uniqueID
            connectedDeviceName = endpoint.displayName
        } catch {
            print("[MIDI] connect failed: \(error)")
        }
    }

    func disconnect() {
        midiManager.remove(.outputConnection, .withTag(Self.connectionTag))
        isConnected = false
        connectedEndpointID = nil
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
        guard let ccNum = UInt7(exactly: cc),
              let ccVal = UInt7(exactly: clamped) else { return }
        sendCC(cc: ccNum, value: ccVal)
    }

    // MARK: - Drawbar 送信

    func updateDrawbar(section: Int, index: Int, value: Int) {
        guard section < 3, index < 9,
              let channel = UInt4(exactly: section),
              let ccNum = UInt7(exactly: 12 + index) else { return }
        let clamped = max(0, min(8, value))
        drawbars[section][index] = clamped
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
            updateFX(cc: cc, value: val)
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
