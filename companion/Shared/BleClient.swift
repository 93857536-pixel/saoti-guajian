import Combine
import CoreBluetooth
import Foundation

#if canImport(UIKit)
import UIKit
#endif
#if canImport(AppKit)
import AppKit
#endif

struct DiscoveredPendant: Identifiable, Hashable {
    let id: UUID
    let name: String
    let rssi: Int
    let peripheral: CBPeripheral

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: DiscoveredPendant, rhs: DiscoveredPendant) -> Bool { lhs.id == rhs.id }
}

@MainActor
final class BleClient: NSObject, ObservableObject {
    enum ConnState: String {
        case poweredOff = "蓝牙已关闭"
        case unauthorized = "无蓝牙权限"
        case scanning = "扫描中…"
        case connecting = "连接中…"
        case connected = "已连接"
        case ready = "就绪"
        case disconnected = "未连接"
    }

    @Published var state: ConnState = .disconnected
    @Published var devices: [DiscoveredPendant] = []
    @Published var status = PendantStatus()
    @Published var lastEvent = ""
    @Published var answerText = ""
    @Published var thumbData: Data?
    @Published var console: [String] = []
    @Published var showSystemBtHint = true

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var cmdChar: CBCharacteristic?
    private var answerBuf = Data()
    private var thumbBuf = Data()
    private var thumbExpected = 0
    private var collectingAnswer = false
    private var scanDeadline: Date?
    private var autoConnectNamePrefix = SaotiBle.namePrefix

    private let defaultsKeyLastId = "saoti.lastPeripheralId"

    let answers = AnswerStore()

    override init() {
        super.init()
        var opts: [String: Any] = [
            CBCentralManagerOptionShowPowerAlertKey: true
        ]
        #if os(iOS)
        opts[CBCentralManagerOptionRestoreIdentifierKey] = "com.saoti.companion.ble"
        #endif
        central = CBCentralManager(delegate: self, queue: nil, options: opts)
    }

    func startScan(autoConnect: Bool = true) {
        devices.removeAll()
        if #available(iOS 13.1, macOS 10.15, *) {
            if CBCentralManager.authorization == .denied {
                state = .unauthorized
                log("请到 系统设置 → 扫题挂件 → 打开蓝牙权限")
                return
            }
        }
        guard central.state == .poweredOn else {
            state = .poweredOff
            log("请先打开手机的系统蓝牙开关，然后回到本 App 扫描（不要在设置里搜设备）")
            return
        }

        // 1) 系统里已经连上的挂件（连着时往往不再广播，扫描会是空的）
        let already = central.retrieveConnectedPeripherals(withServices: [SaotiBle.service])
        for p in already {
            let name = p.name ?? "Saoti"
            let item = DiscoveredPendant(id: p.identifier, name: name, rssi: 0, peripheral: p)
            devices.append(item)
            log("发现已连接设备 \(name)")
            if autoConnect {
                connect(item)
                return
            }
        }

        // 2) 重连上次设备
        if autoConnect, let idStr = UserDefaults.standard.string(forKey: defaultsKeyLastId),
           let uuid = UUID(uuidString: idStr) {
            let known = central.retrievePeripherals(withIdentifiers: [uuid])
            if let p = known.first {
                log("尝试重连上次设备…")
                let item = DiscoveredPendant(
                    id: p.identifier,
                    name: p.name ?? "Saoti",
                    rssi: 0,
                    peripheral: p
                )
                devices = [item]
                connect(item)
                return
            }
        }

        state = .scanning
        scanDeadline = Date().addingTimeInterval(30)
        // 必须扫全部：UUID 常在 Scan Response，按 UUID 过滤会漏
        central.scanForPeripherals(withServices: nil, options: [
            CBCentralManagerScanOptionAllowDuplicatesKey: true
        ])
        log("App 内扫描中…对照挂件屏幕 Saoti-*（系统设置里搜不到属正常）")
    }

    func stopScan() {
        central.stopScan()
        scanDeadline = nil
        if state == .scanning { state = .disconnected }
    }

    func connect(_ device: DiscoveredPendant) {
        stopScan()
        peripheral = device.peripheral
        peripheral?.delegate = self
        state = .connecting
        UserDefaults.standard.set(device.id.uuidString, forKey: defaultsKeyLastId)
        central.connect(device.peripheral, options: [
            CBConnectPeripheralOptionNotifyOnConnectionKey: true,
            CBConnectPeripheralOptionNotifyOnDisconnectionKey: true
        ])
        log("连接 \(device.name)")
    }

    func disconnect() {
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        clearSession()
        state = .disconnected
    }

    func sendCommand(_ cmd: String) {
        guard let p = peripheral, let c = cmdChar,
              let data = cmd.data(using: .utf8) else {
            log("命令失败：未就绪")
            return
        }
        p.writeValue(data, for: c, type: .withResponse)
        log("> \(cmd)")
    }

    func scanQuestion() { sendCommand("scan") }
    func wake() { sendCommand("wake") }
    func requestThumb() {
        thumbData = nil
        thumbBuf.removeAll()
        thumbExpected = 0
        sendCommand("thumb")
    }
    func flash(_ on: Bool) { sendCommand(on ? "flash=1" : "flash=0") }
    func requestAnswer() { sendCommand("answer") }
    func ping() { sendCommand("ping") }

    private func clearSession() {
        peripheral = nil
        cmdChar = nil
        answerBuf.removeAll()
        thumbBuf.removeAll()
        thumbExpected = 0
    }

    func log(_ msg: String) {
        let line = "\(Self.time()) \(msg)"
        console.append(line)
        if console.count > 200 { console.removeFirst(console.count - 200) }
        #if DEBUG
        print(line)
        #endif
    }

    private static func time() -> String {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f.string(from: Date())
    }

    private func handleStatus(_ data: Data) {
        if let parsed = PendantStatus.parse(data) {
            status = parsed
        }
    }

    private func handleAnswerChunk(_ data: Data) {
        if let s = String(data: data, encoding: .utf8), s.hasPrefix("LEN:") {
            answerBuf.removeAll()
            collectingAnswer = true
            if let nl = s.firstIndex(of: "\n") {
                let rest = String(s[s.index(after: nl)...])
                answerBuf.append(Data(rest.utf8))
            }
            return
        }
        if collectingAnswer {
            answerBuf.append(data)
            if let text = String(data: answerBuf, encoding: .utf8), !text.isEmpty {
                answerText = text
                answers.push(text)
            }
        } else if let text = String(data: data, encoding: .utf8) {
            answerText = text
            answers.push(text)
        }
    }

    private func handleThumb(_ data: Data) {
        if data.count >= 8,
           data[0] == 0x54, data[1] == 0x48, data[2] == 0x4D, data[3] == 0x42 {
            thumbExpected = Int(data[4]) | (Int(data[5]) << 8) | (Int(data[6]) << 16) | (Int(data[7]) << 24)
            thumbBuf.removeAll(keepingCapacity: true)
            return
        }
        thumbBuf.append(data)
        if thumbExpected > 0 && thumbBuf.count >= thumbExpected {
            thumbData = thumbBuf.prefix(thumbExpected)
            thumbBuf.removeAll()
            thumbExpected = 0
            log("缩略图接收完成")
        }
    }

    private func handleEvent(_ data: Data) {
        if let s = String(data: data, encoding: .utf8) {
            lastEvent = s
            log("事件 \(s)")
        }
    }
}

extension BleClient: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            switch central.state {
            case .poweredOn:
                self.log("蓝牙已打开 — 请在本 App「设备」页扫描，不要用系统设置搜挂件")
                if self.state == .poweredOff || self.state == .unauthorized || self.state == .disconnected {
                    self.startScan(autoConnect: true)
                }
            case .poweredOff:
                self.state = .poweredOff
                self.log("系统蓝牙已关闭：先打开蓝牙开关，再回 App")
            case .unauthorized:
                self.state = .unauthorized
                self.log("无蓝牙权限：设置 → 扫题挂件 → 蓝牙")
            case .unsupported:
                self.state = .poweredOff
                self.log("此设备不支持 BLE（模拟器也不行，请用真机）")
            default:
                break
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
            let localName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
            let name = peripheral.name ?? localName ?? ""
            let uuids = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
            let mfg = advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data
            let hasService = uuids.contains(SaotiBle.service)
            let nameHit = name.hasPrefix(SaotiBle.namePrefix) || name == "Saoti"
            let mfgHit = SaotiBle.matchesManufacturer(mfg)
            guard nameHit || hasService || mfgHit else { return }

            let display: String = {
                if nameHit { return name }
                if !name.isEmpty { return name }
                return "Saoti"
            }()
            let item = DiscoveredPendant(
                id: peripheral.identifier,
                name: display,
                rssi: RSSI.intValue,
                peripheral: peripheral
            )
            if let idx = self.devices.firstIndex(where: { $0.id == item.id }) {
                self.devices[idx] = item
            } else {
                self.devices.append(item)
                self.devices.sort { $0.rssi > $1.rssi }
                self.log("发现 \(display)  \(RSSI.intValue) dBm")
                // 自动连接最强信号的 Saoti
                if self.state == .scanning, display.hasPrefix(self.autoConnectNamePrefix) {
                    self.connect(item)
                }
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            self.state = .connected
            self.log("已连接，发现服务…")
            peripheral.discoverServices([SaotiBle.service])
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: Error?
    ) {
        Task { @MainActor in
            self.state = .disconnected
            self.log("连接失败: \(error?.localizedDescription ?? "?") — 重新扫描")
            self.startScan(autoConnect: false)
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        error: Error?
    ) {
        Task { @MainActor in
            self.clearSession()
            self.state = .disconnected
            self.log("已断开 — 3 秒后自动重连")
            try? await Task.sleep(nanoseconds: 3_000_000_000)
            if self.state == .disconnected {
                self.startScan(autoConnect: true)
            }
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager,
        willRestoreState dict: [String: Any]
    ) {
        Task { @MainActor in
            if let list = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
               let p = list.first {
                self.peripheral = p
                self.peripheral?.delegate = self
                self.state = .connecting
                if p.state == .connected {
                    p.discoverServices([SaotiBle.service])
                } else {
                    central.connect(p, options: nil)
                }
            }
        }
    }
}

extension BleClient: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        Task { @MainActor in
            guard let services = peripheral.services else { return }
            for s in services where s.uuid == SaotiBle.service {
                peripheral.discoverCharacteristics([
                    SaotiBle.status, SaotiBle.command, SaotiBle.answer,
                    SaotiBle.thumb, SaotiBle.event
                ], for: s)
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
            for c in chars {
                switch c.uuid {
                case SaotiBle.command:
                    self.cmdChar = c
                case SaotiBle.status, SaotiBle.answer, SaotiBle.thumb, SaotiBle.event:
                    peripheral.setNotifyValue(true, for: c)
                    if c.uuid == SaotiBle.status || c.uuid == SaotiBle.answer {
                        peripheral.readValue(for: c)
                    }
                default:
                    break
                }
            }
            self.state = .ready
            self.log("GATT 就绪")
            self.sendCommand("ping")
            self.sendCommand("status")
        }
    }

    nonisolated func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        Task { @MainActor in
            guard let data = characteristic.value else { return }
            switch characteristic.uuid {
            case SaotiBle.status:
                self.handleStatus(data)
            case SaotiBle.answer:
                self.handleAnswerChunk(data)
            case SaotiBle.thumb:
                self.handleThumb(data)
            case SaotiBle.event:
                self.handleEvent(data)
            default:
                break
            }
        }
    }
}
