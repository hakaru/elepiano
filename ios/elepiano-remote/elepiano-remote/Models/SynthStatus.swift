import Foundation

struct SynthStatus: Codable, Equatable {
    let running: Bool
    let program: Int?
    let name: String?
    let voices: Int?
    let underruns: UInt32?
    let elepianoConnected: Bool?

    enum CodingKeys: String, CodingKey {
        case running
        case program
        case name
        case voices
        case underruns
        case elepianoConnected = "elepiano_connected"
    }

    var isReady: Bool {
        running && (elepianoConnected ?? false)
    }
}
