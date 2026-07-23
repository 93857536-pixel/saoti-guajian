import Combine
import CoreGraphics
import Darwin
import Foundation
import IOKit
#if canImport(AppKit)
import AppKit
#endif

private let kIOSSIOSPEED: UInt = 0x8004_5402

final class SerialStreamClient: ObservableObject {
    @Published private(set) var currentFrame: PlatformImage?
    @Published private(set) var displayCGImage: CGImage?
    @Published private(set) var frameSequence: Int = 0
    @Published private(set) var frameDebugInfo: String = ""
    @Published private(set) var isConnected = false
    @Published private(set) var statusText = "选择 USB 串口后点击连接"
    @Published private(set) var frameCount: Int = 0
    @Published private(set) var decodedCount: Int = 0
    @Published private(set) var packetCount: Int = 0
    @Published private(set) var bytesReceived: Int = 0
    @Published private(set) var availablePorts: [String] = []

    private var fd: Int32 = -1
    private let serialQueue = DispatchQueue(label: "com.saoti.serial", qos: .userInitiated)
    private let decodeQueue = DispatchQueue(label: "com.saoti.decode", qos: .userInitiated)
    private var pendingDecode: (payload: Data, type: UInt8)?
    private var decodeWorkerRunning = false
    private var connectionID: UInt64 = 0
    private var shouldRun = false
    private var buffer = Data()
    private var fpsCounter = 0
    private var bytesCounter = 0
    private var fpsTimer: Timer?
    private var keepaliveTimer: Timer?
    private var lastPacketMode = ""

    private let magicPrefix = Data([0x53, 0x43, 0x01])
    private let magicJpeg = Data([0x53, 0x43, 0x01, 0xFE])
    private let magicRgb565 = Data([0x53, 0x43, 0x01, 0xFC])
    private let baudRate: speed_t = 921600

    func refreshPorts() {
        availablePorts = Self.discoverPorts()
    }

    func connect(portPath: String) {
        disconnect()
        refreshPorts()

        let path = portPath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !path.isEmpty else {
            statusText = "请选择串口"
            return
        }

        fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        if fd < 0 {
            statusText = "无法打开 \(path)"
            return
        }

        guard configurePort(fd) else {
            close(fd)
            fd = -1
            statusText = "串口配置失败"
            return
        }

        clearModemLines(fd)
        buffer.removeAll(keepingCapacity: true)
        fpsCounter = 0
        frameCount = 0
        decodedCount = 0
        packetCount = 0
        bytesReceived = 0
        bytesCounter = 0
        shouldRun = true
        isConnected = true
        statusText = "USB 已连接，等待设备就绪..."

        connectionID &+= 1
        let activeID = connectionID
        serialQueue.async { [weak self] in
            guard let self, self.connectionID == activeID else { return }
            let ready = self.waitForUSBReady(timeoutSeconds: 12)
            guard self.connectionID == activeID, self.shouldRun, self.fd >= 0 else { return }
            if !ready {
                DispatchQueue.main.async {
                    self.statusText = "未等到 USB ready，仍尝试启动推流（请确认推流固件）"
                }
            }
            _ = self.writeBytes([0x76])
            usleep(200_000)
            self.drainInput(forMilliseconds: 100)
            guard self.connectionID == activeID, self.shouldRun, self.fd >= 0 else { return }
            guard self.writeBytes([0x56]) else {
                DispatchQueue.main.async {
                    self.statusText = "无法发送启动命令 (V)"
                }
                return
            }
            DispatchQueue.main.async {
                self.statusText = "USB 已连接，等待画面..."
            }
            self.readLoop(connectionID: activeID)
        }

        fpsTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            guard let self else { return }
            DispatchQueue.main.async {
                self.frameCount = self.fpsCounter
                self.fpsCounter = 0
                self.bytesReceived = self.bytesCounter
                self.bytesCounter = 0
            }
        }
        RunLoop.main.add(fpsTimer!, forMode: .common)

        keepaliveTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            guard let self, self.isConnected, self.decodedCount == 0 else { return }
            self.serialQueue.async {
                guard self.shouldRun, self.fd >= 0 else { return }
                _ = self.writeBytes([0x56])
            }
        }
        RunLoop.main.add(keepaliveTimer!, forMode: .common)

        DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in
            guard let self, self.isConnected, self.currentFrame == nil else { return }
            if self.decodedCount > 0 {
                return
            }
            if self.packetCount > 0 {
                self.statusText = "收到 \(self.packetCount) 包，解码失败 \(self.packetCount - self.decodedCount) 次"
            } else if self.bytesReceived > 0 {
                self.statusText = "收到 \(self.bytesReceived) B/s 但无有效帧头"
            } else {
                self.statusText = "未收到数据：确认外接 USB 转串口"
            }
        }
    }

    func disconnect() {
        connectionID &+= 1
        if fd >= 0 {
            serialQueue.async { [weak self] in
                _ = self?.writeBytes([0x76])  // 'v' stop stream
            }
        }

        shouldRun = false
        fpsTimer?.invalidate()
        fpsTimer = nil
        keepaliveTimer?.invalidate()
        keepaliveTimer = nil

        if fd >= 0 {
            close(fd)
            fd = -1
        }

        serialQueue.async { [weak self] in
            self?.buffer.removeAll(keepingCapacity: false)
        }

        DispatchQueue.main.async {
            self.isConnected = false
            self.currentFrame = nil
            self.displayCGImage = nil
            self.frameDebugInfo = ""
            self.frameCount = 0
            self.decodedCount = 0
            self.packetCount = 0
            self.bytesReceived = 0
            self.bytesCounter = 0
            self.statusText = "已断开"
        }
    }

    // MARK: - Serial I/O

    private func configurePort(_ fd: Int32) -> Bool {
        var options = termios()
        guard tcgetattr(fd, &options) == 0 else { return false }

        cfmakeraw(&options)
        options.c_cflag |= UInt(CLOCAL | CREAD)
        options.c_cflag &= ~UInt(PARENB)
        options.c_cflag &= ~UInt(CSTOPB)
        options.c_cflag &= ~UInt(CSIZE)
        options.c_cflag |= UInt(CS8)
        options.c_cc.16 = 0  // VMIN — non-blocking poll
        options.c_cc.17 = 0  // VTIME

        guard tcsetattr(fd, TCSANOW, &options) == 0 else { return false }

        // Custom baud must be applied AFTER tcsetattr (pyserial/macOS requirement).
        var speed = baudRate
        if ioctl(fd, CUnsignedLong(kIOSSIOSPEED), &speed) == -1 {
            DispatchQueue.main.async {
                self.statusText = "921600 设置失败，已回退 115200（推流可能无画面）"
            }
            var fallback = termios()
            guard tcgetattr(fd, &fallback) == 0 else { return false }
            cfsetispeed(&fallback, speed_t(B115200))
            cfsetospeed(&fallback, speed_t(B115200))
            guard tcsetattr(fd, TCSANOW, &fallback) == 0 else { return false }
        }

        return true
    }

    private func clearModemLines(_ fd: Int32) {
        // Some adapters reject TIOCM* ioctls; ignore failures.
    }

    private func waitForUSBReady(timeoutSeconds: Int) -> Bool {
        DispatchQueue.main.async {
            self.statusText = "USB 已连接，等待摄像头就绪..."
        }
        let marker = Data("[USB] ready".utf8)
        var scratch = Data()
        let deadline = DispatchTime.now().uptimeNanoseconds + UInt64(timeoutSeconds) * 1_000_000_000
        var tmp = [UInt8](repeating: 0, count: 4096)
        while fd >= 0, DispatchTime.now().uptimeNanoseconds < deadline {
            let n = read(fd, &tmp, tmp.count)
            if n > 0 {
                scratch.append(tmp, count: n)
                if scratch.count > 8192 {
                    scratch.removeFirst(scratch.count - 8192)
                }
                if scratch.range(of: marker) != nil {
                    return true
                }
                if scratch.range(of: Data("camera=FAIL".utf8)) != nil
                    || scratch.range(of: Data("camera fail".utf8)) != nil {
                    DispatchQueue.main.async {
                        self.statusText = "摄像头初始化失败，请断电重插 ESP32"
                    }
                    return false
                }
                continue
            }
            if n < 0 {
                let err = errno
                if err != EAGAIN && err != EWOULDBLOCK {
                    break
                }
            }
            usleep(20_000)
        }
        return false
    }

    private func collectBootData(timeoutSeconds: Int) -> Data {
        DispatchQueue.main.async {
            self.statusText = "USB 已连接，等待设备..."
        }
        var scratch = Data()
        let deadline = DispatchTime.now().uptimeNanoseconds + UInt64(timeoutSeconds) * 1_000_000_000
        var tmp = [UInt8](repeating: 0, count: 4096)
        while fd >= 0, DispatchTime.now().uptimeNanoseconds < deadline {
            let n = read(fd, &tmp, tmp.count)
            if n > 0 {
                scratch.append(tmp, count: n)
                if scratch.count > 16384 {
                    scratch.removeFirst(scratch.count - 16384)
                }
                if scratch.range(of: Data("[USB] ready".utf8)) != nil
                    || scratch.range(of: Data("[APP] ready".utf8)) != nil {
                    break
                }
                continue
            }
            if n < 0 {
                let err = errno
                if err != EAGAIN && err != EWOULDBLOCK {
                    break
                }
            }
            usleep(20_000)
        }
        return scratch
    }

    private func drainInput(forMilliseconds ms: Int) {
        let deadline = DispatchTime.now().uptimeNanoseconds + UInt64(ms) * 1_000_000
        var tmp = [UInt8](repeating: 0, count: 4096)
        while fd >= 0, DispatchTime.now().uptimeNanoseconds < deadline {
            let n = read(fd, &tmp, tmp.count)
            if n > 0 {
                continue
            }
            if n < 0 {
                let err = errno
                if err != EAGAIN && err != EWOULDBLOCK {
                    break
                }
            }
            usleep(5000)
        }
    }

    @discardableResult
    private func writeBytes(_ bytes: [UInt8]) -> Bool {
        guard fd >= 0 else { return false }
        return bytes.withUnsafeBytes { raw in
            guard let base = raw.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                return false
            }
            var sent = 0
            while sent < bytes.count {
                let n = write(fd, base.advanced(by: sent), bytes.count - sent)
                if n > 0 {
                    sent += n
                } else if n < 0 && errno == EAGAIN {
                    usleep(1000)
                } else {
                    return false
                }
            }
            return true
        }
    }

    private func readLoop(connectionID: UInt64) {
        var chunk = [UInt8](repeating: 0, count: 8192)
        while shouldRun, fd >= 0, self.connectionID == connectionID {
            let n = read(fd, &chunk, chunk.count)
            if n > 0 {
                buffer.append(chunk, count: n)
                bytesCounter += n
                processBuffer()
            } else if n == 0 {
                usleep(2000)
            } else {
                let err = errno
                if err == EAGAIN || err == EWOULDBLOCK {
                    usleep(2000)
                    continue
                }
                DispatchQueue.main.async {
                    self.statusText = "读取失败 (\(err))"
                    self.isConnected = false
                }
                break
            }
        }
    }

    // MARK: - Frame parsing

    private func readUInt32LE(from offset: Int) -> UInt32? {
        guard offset >= 0, buffer.count >= offset + 4 else { return nil }
        return buffer.withUnsafeBytes { raw in
            guard raw.count >= offset + 4, let base = raw.baseAddress else { return nil }
            return base.advanced(by: offset).loadUnaligned(as: UInt32.self)
        }
    }

    private func processBuffer() {
        let headerSize = 8
        while true {
            guard let magicRange = buffer.range(of: magicPrefix) else {
                if buffer.count > 4096 {
                    buffer.removeFirst(buffer.count - 128)
                }
                return
            }

            if magicRange.lowerBound > buffer.startIndex {
                buffer.removeSubrange(..<magicRange.lowerBound)
            }

            guard buffer.count >= headerSize else { return }

            let typeIndex = buffer.index(buffer.startIndex, offsetBy: 3)
            let packetType = buffer[typeIndex]
            guard packetType == 0xFE || packetType == 0xFC else {
                buffer.removeFirst(1)
                continue
            }

            guard let length = readUInt32LE(from: 4) else { return }
            guard length >= 128, length < 512_000 else {
                buffer.removeFirst(1)
                continue
            }

            let totalFrameSize = headerSize + Int(length)
            guard buffer.count >= totalFrameSize else { return }

            let payloadStart = buffer.index(buffer.startIndex, offsetBy: headerSize)
            let payloadEnd = buffer.index(payloadStart, offsetBy: Int(length))
            let payload = buffer.subdata(in: payloadStart..<payloadEnd)
            buffer.removeSubrange(
                ..<buffer.index(buffer.startIndex, offsetBy: totalFrameSize)
            )

            let modeLabel = packetType == 0xFC ? "RGB565 渲染" : "JPEG 解码"
            lastPacketMode = modeLabel

            DispatchQueue.main.async {
                self.fpsCounter += 1
                self.packetCount += 1
                if self.decodedCount == 0, self.packetCount % 5 == 0 {
                    self.statusText = "收包 \(self.packetCount)，\(modeLabel)中..."
                }
            }

            decodeQueue.async { [weak self] in
                guard let self else { return }
                self.pendingDecode = (payload, packetType)
                guard !self.decodeWorkerRunning else { return }
                self.decodeWorkerRunning = true
                while let job = self.pendingDecode {
                    self.pendingDecode = nil
                    let cgImage: CGImage?
                    if job.type == 0xFC {
                        cgImage = PlatformImage.cgImageFromRGB565Payload(job.payload)
                    } else {
                        guard job.payload.count >= 2, job.payload[0] == 0xFF, job.payload[1] == 0xD8 else {
                            continue
                        }
                        cgImage = PlatformImage.cgImageFromJPEGData(job.payload)
                    }
                    if let cgImage {
                        DispatchQueue.main.async {
                            self.displayCGImage = cgImage
                            #if canImport(AppKit)
                            self.currentFrame = NSImage(
                                cgImage: cgImage,
                                size: NSSize(width: cgImage.width, height: cgImage.height)
                            )
                            #endif
                            self.frameSequence += 1
                            self.decodedCount += 1
                            self.frameDebugInfo = "\(cgImage.width)×\(cgImage.height) #\(self.frameSequence)"
                            self.statusText = "USB 直播中 \(cgImage.width)×\(cgImage.height)"
                        }
                    }
                }
                self.decodeWorkerRunning = false
            }
        }
    }

    private static func discoverPorts() -> [String] {
        guard let entries = try? FileManager.default.contentsOfDirectory(atPath: "/dev") else {
            return []
        }
        return entries
            .filter {
                $0.hasPrefix("cu.usb") || $0.hasPrefix("cu.wchusb") || $0.hasPrefix("cu.SLAB")
                    || $0.hasPrefix("cu.usbmodem") || $0.hasPrefix("cu.usbserial")
            }
            .map { "/dev/\($0)" }
            .sorted()
    }
}
