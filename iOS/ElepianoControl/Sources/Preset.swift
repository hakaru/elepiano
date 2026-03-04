import Foundation

struct Preset: Codable, Identifiable {
    var id = UUID()
    var name: String
    var mode: EngineMode
    var fxValues: [Int: Int]
    var drawbars: [[Int]]
    var leslieFast: Bool
}

struct PresetStore {
    private let key = "elepiano_presets"

    func loadAll() -> [Preset] {
        guard let data = UserDefaults.standard.data(forKey: key),
              let presets = try? JSONDecoder().decode([Preset].self, from: data)
        else { return [] }
        return presets
    }

    func saveAll(_ presets: [Preset]) {
        guard let data = try? JSONEncoder().encode(presets) else { return }
        UserDefaults.standard.set(data, forKey: key)
    }
}
