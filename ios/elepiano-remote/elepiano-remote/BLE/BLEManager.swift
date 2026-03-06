import CoreBluetooth
import Combine

@MainActor
final class BLEManager: NSObject, ObservableObject {
    @Published var isScanning = false
    @Published var isConnected = false
    @Published var peripheralName: String?
    @Published var status: SynthStatus?

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var ccCharacteristic: CBCharacteristic?
    private var pcCharacteristic: CBCharacteristic?
    private var statusCharacteristic: CBCharacteristic?
    private var batchCCCharacteristic: CBCharacteristic?

    // CC スロットリング: 同じ CC を短期間に送りすぎない
    private var lastCCSendTime: [Int: Date] = [:]
    private let ccMinInterval: TimeInterval = 0.025  // 40Hz max

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func startScan() {
        guard centralManager.state == .poweredOn else { return }
        isScanning = true
        centralManager.scanForPeripherals(
            withServices: [BLEConstants.serviceUUID],
            options: nil
        )
        // 10秒後に自動停止
        DispatchQueue.main.asyncAfter(deadline: .now() + 10) { [weak self] in
            self?.stopScan()
        }
    }

    func stopScan() {
        centralManager.stopScan()
        isScanning = false
    }

    func disconnect() {
        guard let peripheral else { return }
        centralManager.cancelPeripheralConnection(peripheral)
    }

    // MARK: - CC/PC 送信

    func sendCC(_ cc: Int, value: Int) {
        guard let char = ccCharacteristic, let peripheral else { return }

        // スロットリング
        let now = Date()
        if let last = lastCCSendTime[cc], now.timeIntervalSince(last) < ccMinInterval {
            return
        }
        lastCCSendTime[cc] = now

        let data = Data([UInt8(cc & 0x7F), UInt8(value & 0x7F)])
        peripheral.writeValue(data, for: char, type: .withoutResponse)
    }

    func sendBatchCC(_ pairs: [(cc: Int, value: Int)]) {
        guard let char = batchCCCharacteristic, let peripheral else { return }
        var bytes: [UInt8] = []
        for pair in pairs.prefix(10) {  // max 20 bytes = 10 pairs
            bytes.append(UInt8(pair.cc & 0x7F))
            bytes.append(UInt8(pair.value & 0x7F))
        }
        peripheral.writeValue(Data(bytes), for: char, type: .withoutResponse)
    }

    func sendProgramChange(_ program: Int) {
        guard let char = pcCharacteristic, let peripheral else { return }
        let data = Data([UInt8(program & 0x7F)])
        peripheral.writeValue(data, for: char, type: .withResponse)
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            if central.state == .poweredOn {
                startScan()
            }
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        Task { @MainActor in
            stopScan()
            self.peripheral = peripheral
            self.peripheralName = peripheral.name ?? "elepiano"
            peripheral.delegate = self
            central.connect(peripheral, options: nil)
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager,
        didConnect peripheral: CBPeripheral
    ) {
        Task { @MainActor in
            isConnected = true
            peripheral.discoverServices([BLEConstants.serviceUUID])
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        error: Error?
    ) {
        Task { @MainActor in
            isConnected = false
            self.peripheral = nil
            ccCharacteristic = nil
            pcCharacteristic = nil
            statusCharacteristic = nil
            batchCCCharacteristic = nil
            status = nil

            // 自動再接続
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
                self?.startScan()
            }
        }
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {
    nonisolated func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverServices error: Error?
    ) {
        Task { @MainActor in
            guard let services = peripheral.services else { return }
            for service in services {
                peripheral.discoverCharacteristics(nil, for: service)
            }
        }
    }

    nonisolated func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        Task { @MainActor in
            guard let chars = service.characteristics else { return }
            for char in chars {
                switch char.uuid {
                case BLEConstants.statusUUID:
                    statusCharacteristic = char
                    peripheral.setNotifyValue(true, for: char)
                case BLEConstants.ccControlUUID:
                    ccCharacteristic = char
                case BLEConstants.programChangeUUID:
                    pcCharacteristic = char
                case BLEConstants.batchCCUUID:
                    batchCCCharacteristic = char
                default:
                    break
                }
            }
        }
    }

    nonisolated func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        Task { @MainActor in
            guard let data = characteristic.value else { return }
            if characteristic.uuid == BLEConstants.statusUUID {
                if let decoded = try? JSONDecoder().decode(SynthStatus.self, from: data) {
                    status = decoded
                }
            }
        }
    }
}
