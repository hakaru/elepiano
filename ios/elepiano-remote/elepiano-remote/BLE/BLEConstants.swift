import CoreBluetooth

enum BLEConstants {
    static let serviceUUID = CBUUID(string: "e1e00000-0001-4000-8000-00805f9b34fb")
    static let statusUUID = CBUUID(string: "e1e00001-0001-4000-8000-00805f9b34fb")
    static let ccControlUUID = CBUUID(string: "e1e00002-0001-4000-8000-00805f9b34fb")
    static let programChangeUUID = CBUUID(string: "e1e00003-0001-4000-8000-00805f9b34fb")
    static let audioDeviceUUID = CBUUID(string: "e1e00004-0001-4000-8000-00805f9b34fb")
    static let batchCCUUID = CBUUID(string: "e1e00005-0001-4000-8000-00805f9b34fb")
    static let commandUUID = CBUUID(string: "e1e00006-0001-4000-8000-00805f9b34fb")
}
