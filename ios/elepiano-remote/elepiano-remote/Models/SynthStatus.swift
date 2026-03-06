import Foundation

struct SynthStatus: Codable, Equatable {
    let running: Bool
    let program: Int
    let name: String
    let voices: Int
    let underruns: UInt32
}
