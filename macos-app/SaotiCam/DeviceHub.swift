import Combine
import Darwin
import Foundation
import IOKit

private let kIOSSIOSPEED: UInt = 0x8004_5402

/// USB 串口设备中枢：状态轮询、4G 配置、诊断命令。
final class DeviceHub: ObservableObject {
    enum BaudChoice: Int, CaseIterable, Identifiable {
        case diag = 115200
        case stream = 921600
        var id: Int { rawValue }
        var speed: speed_t { speed_t(rawValue) }
    }

    @Published private(set) var availablePorts: [String] = []
    @Published var selectedPort = ""
    @Published var baudChoice: BaudChoice = .diag
    @Published private(set) var isConnected = false
    @Published private(set) var statusText = "选择串口后连接设备"
    @Published private(set) var snapshot = DeviceSnapshot()
    @Published private(set) var consoleLines: [String] = []
    @Published private(set) var isBusy = false
    @Published var apnDraft = "3gnet"

    private var fd: Int32 = -1
    private let queue = DispatchQueue(label: "com.saoti.devicehub", qos: .userInitiated)
    private var shouldRun = false
    private var connectionID: UInt64 = 0
    private var pollTimer: Timer?
    private var lineBuffer = Data()

    private let maxConsole = 400

    func refreshPorts() {
        availablePorts = Self.discoverPorts()
        if selectedPort.isEmpty || !availablePorts.contains(selectedPort) {
            selectedPort = preferredPort(from: availablePorts)
        }
        if availablePorts.isEmpty {
            statusText = "未检测到 USB 串口 — 请插紧 CH343/USB 线后点刷新"
            appendConsole("— 未找到 /dev/cu.usb* 设备 —")
        } else if !isConnected {
            statusText = "已找到 \(availablePorts.count) 个串口，点「连接」"
        }
    }

    /// 刷新串口；若找到设备且当前未连接，则自动尝试连接。
    func refreshAndConnectIfPossible() {
        refreshPorts()
        guard !isConnected, !selectedPort.isEmpty else { return }
        connect()
    }

    func connect(portPath: String? = nil) {
        // 先干净断开，但不要把 status 立刻刷成「已断开」干扰本次连接提示
        pollTimer?.invalidate()
        pollTimer = nil
        shouldRun = false
        connectionID &+= 1
        if fd >= 0 {
            close(fd)
            fd = -1
        }
        isConnected = false

        refreshPorts()
        let path = (portPath ?? selectedPort).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !path.isEmpty else {
            statusText = "未检测到 USB 串口 — 请检查数据线/驱动后点刷新"
            appendConsole("— 连接失败：无可用串口 —")
            return
        }
        selectedPort = path

        // 设备节点可能刚插上，稍等再 open
        if !FileManager.default.fileExists(atPath: path) {
            statusText = "串口节点不存在：\(path)"
            appendConsole("— 文件不存在 \(path) —")
            return
        }

        fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        if fd < 0 {
            let err = String(cString: strerror(errno))
            statusText = "无法打开 \(path)（\(err)）"
            appendConsole("— open 失败：\(err) —")
            return
        }
        if !configurePort(fd) {
            // ioctl 失败时仍继续：部分适配器仍可按默认波特率通信
            appendConsole("— 警告：波特率 ioctl 未成功，继续尝试 —")
        }

        shouldRun = true
        isConnected = true
        statusText = "已连接 · 正在读取状态"
        let cid = connectionID
        lineBuffer.removeAll(keepingCapacity: true)
        appendConsole("— 已连接 \(path) @ \(baudChoice.rawValue) —")

        queue.async { [weak self] in
            self?.readLoop(connectionID: cid)
        }

        // 给固件一点时间后再问状态
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { [weak self] in
            self?.requestStatus()
        }
        startPolling()
    }

    func disconnect() {
        pollTimer?.invalidate()
        pollTimer = nil
        shouldRun = false
        connectionID &+= 1
        if fd >= 0 {
            close(fd)
            fd = -1
        }
        isConnected = false
        isBusy = false
        statusText = "已断开"
        snapshot = DeviceSnapshot()
        appendConsole("— 已断开 —")
    }

    func requestStatus() {
        sendLine("?")
    }

    func applyAPN(_ apn: String) {
        let trimmed = apn.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        isBusy = true
        statusText = "正在写入 APN…"
        sendLine("APN=\(trimmed)")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
            self?.requestStatus()
            self?.isBusy = false
        }
    }

    func runCellularDiag() {
        isBusy = true
        statusText = "正在诊断 4G…"
        sendLine("DIAG")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
            self?.isBusy = false
            self?.statusText = "诊断已发送，查看控制台"
            self?.requestStatus()
        }
    }

    func runUARTScan() {
        isBusy = true
        statusText = "正在扫描 UART 脚位（约 1–3 分钟）…"
        sendLine("SCAN")
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
            self?.isBusy = false
            self?.statusText = "扫描进行中，请看控制台"
        }
    }

    func runNetAttach() {
        isBusy = true
        statusText = "正在附着网络…"
        sendLine("NET")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
            self?.isBusy = false
            self?.requestStatus()
        }
    }

    func sendRaw(_ text: String) {
        sendLine(text)
    }

    // MARK: - Private

    private func startPolling() {
        pollTimer?.invalidate()
        pollTimer = Timer.scheduledTimer(withTimeInterval: 3.0, repeats: true) { [weak self] _ in
            guard let self, self.isConnected, !self.isBusy else { return }
            self.requestStatus()
        }
    }

    private func sendLine(_ line: String) {
        let payload = line.hasSuffix("\n") ? line : line + "\n"
        guard let data = payload.data(using: .utf8) else { return }
        queue.async { [weak self] in
            guard let self, self.fd >= 0 else { return }
            _ = data.withUnsafeBytes { raw in
                guard let base = raw.bindMemory(to: UInt8.self).baseAddress else { return 0 }
                return write(self.fd, base, data.count)
            }
            DispatchQueue.main.async {
                self.appendConsole("> \(line)")
            }
        }
    }

    private func readLoop(connectionID: UInt64) {
        var buf = [UInt8](repeating: 0, count: 4096)
        while shouldRun, connectionID == self.connectionID, fd >= 0 {
            let n = read(fd, &buf, buf.count)
            if n > 0 {
                let chunk = Data(buf.prefix(n))
                DispatchQueue.main.async { [weak self] in
                    self?.ingest(chunk)
                }
            } else if n == 0 {
                usleep(20_000)
            } else {
                if errno == EAGAIN || errno == EWOULDBLOCK {
                    usleep(20_000)
                } else {
                    DispatchQueue.main.async { [weak self] in
                        self?.statusText = "串口读取中断"
                        self?.disconnect()
                    }
                    break
                }
            }
        }
    }

    private func ingest(_ chunk: Data) {
        lineBuffer.append(chunk)
        while let range = lineBuffer.range(of: Data([0x0A])) {
            let lineData = lineBuffer.subdata(in: lineBuffer.startIndex..<range.lowerBound)
            lineBuffer.removeSubrange(lineBuffer.startIndex..<range.upperBound)
            var text = String(data: lineData, encoding: .utf8) ?? ""
            if text.hasSuffix("\r") { text.removeLast() }
            text = text.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !text.isEmpty else { continue }
            appendConsole(text)
            parseLine(text)
        }
        if lineBuffer.count > 64_000 {
            lineBuffer.removeFirst(lineBuffer.count - 1024)
        }
    }

    private func parseLine(_ line: String) {
        guard line.hasPrefix("{"), line.hasSuffix("}"),
              let data = line.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let type = obj["type"] as? String else { return }

        switch type {
        case "status":
            var next = snapshot
            next.camera = boolHealth(obj["cam"])
            next.display = boolHealth(obj["lcd"])
            next.cellular = boolHealth(obj["cell"])
            next.wifi = boolHealth(obj["wifi"])
            next.usbStream = boolHealth(obj["usb"], offlineAs: .offline)
            if let apn = obj["apn"] as? String, !apn.isEmpty {
                next.apn = apn
                if apnDraft.isEmpty || apnDraft == "3gnet" || apnDraft == snapshot.apn {
                    apnDraft = apn
                }
            }
            if let csq = obj["csq"] as? Int {
                next.csq = csq
            } else if let csq = obj["csq"] as? NSNumber {
                next.csq = csq.intValue
            }
            next.streamEnabled = (obj["stream"] as? Bool) ?? false
            next.mockCamera = intFlag(obj["mock_cam"])
            next.mockDisplay = intFlag(obj["mock_lcd"])
            next.mockModem = intFlag(obj["mock_net"])
            next.updatedAt = Date()
            snapshot = next
            statusText = "已同步 · \(Self.timeString(next.updatedAt))"
        case "apn":
            let ok = (obj["ok"] as? Bool) ?? false
            if ok, let apn = obj["apn"] as? String {
                snapshot.apn = apn
                apnDraft = apn
                statusText = "APN 已更新为 \(apn)"
            } else {
                statusText = "APN 写入失败"
            }
        case "net":
            let ok = (obj["ok"] as? Bool) ?? false
            statusText = ok ? "蜂窝网络已附着" : "蜂窝附着失败"
            requestStatus()
        default:
            break
        }
    }

    private func boolHealth(_ value: Any?, offlineAs: ModuleHealth = .fail) -> ModuleHealth {
        if let b = value as? Bool {
            return b ? .ok : offlineAs
        }
        if let n = value as? NSNumber {
            return n.boolValue ? .ok : offlineAs
        }
        return .unknown
    }

    private func intFlag(_ value: Any?) -> Bool {
        if let b = value as? Bool { return b }
        if let n = value as? NSNumber { return n.intValue != 0 }
        return false
    }

    private func appendConsole(_ line: String) {
        consoleLines.append(line)
        if consoleLines.count > maxConsole {
            consoleLines.removeFirst(consoleLines.count - maxConsole)
        }
    }

    private func configurePort(_ fd: Int32) -> Bool {
        var term = termios()
        guard tcgetattr(fd, &term) == 0 else { return false }
        cfmakeraw(&term)
        term.c_cflag |= tcflag_t(CLOCAL | CREAD | CS8)
        term.c_cflag &= ~tcflag_t(PARENB | CSTOPB | CRTSCTS)
        withUnsafeMutablePointer(to: &term.c_cc) { ptr in
            let cc = UnsafeMutableRawPointer(ptr).assumingMemoryBound(to: cc_t.self)
            cc[Int(VMIN)] = 0
            cc[Int(VTIME)] = 0
        }
        _ = withUnsafeMutablePointer(to: &term) { tcsetattr(fd, TCSANOW, $0) }
        // macOS 自定义波特率
        var speed = baudChoice.speed
        let r = ioctl(fd, kIOSSIOSPEED, &speed)
        if r != 0 {
            // 回退标准波特率设置
            cfsetispeed(&term, speed_t(B115200))
            cfsetospeed(&term, speed_t(B115200))
            _ = withUnsafeMutablePointer(to: &term) { tcsetattr(fd, TCSANOW, $0) }
        }
        // 即使 ioctl 失败也允许继续（避免一直「未连接」）
        return true
    }

    private static func discoverPorts() -> [String] {
        guard let entries = try? FileManager.default.contentsOfDirectory(atPath: "/dev") else {
            return []
        }
        return entries
            .filter {
                $0.hasPrefix("cu.usb")
                    || $0.hasPrefix("cu.wchusb")
                    || $0.hasPrefix("cu.SLAB")
                    || $0.hasPrefix("cu.usbmodem")
                    || $0.hasPrefix("cu.usbserial")
            }
            .map { "/dev/\($0)" }
            .sorted()
    }

    private func preferredPort(from ports: [String]) -> String {
        ports.first(where: { $0.contains("usbserial") })
            ?? ports.first(where: { !$0.contains("usbmodem") })
            ?? ports.first
            ?? ""
    }

    private static func timeString(_ date: Date?) -> String {
        guard let date else { return "—" }
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f.string(from: date)
    }
}
