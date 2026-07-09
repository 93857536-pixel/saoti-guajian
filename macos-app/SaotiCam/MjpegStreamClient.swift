import Combine
import Foundation

final class MjpegStreamClient: NSObject, ObservableObject, URLSessionDataDelegate {
    @Published private(set) var currentFrame: PlatformImage?
    @Published private(set) var isConnected = false
    @Published private(set) var statusText = "输入 IP 后点击连接"
    @Published private(set) var frameCount: Int = 0

    private var session: URLSession?
    private var task: URLSessionDataTask?
    private var buffer = Data()
    private var boundary: Data?
    private var fpsCounter = 0
    private var fpsTimer: Timer?

    func connect(host: String) {
        disconnect()

        let trimmed = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            statusText = "IP 不能为空"
            return
        }

        guard let url = URL(string: "http://\(trimmed)/stream") else {
            statusText = "无效的 IP 地址"
            return
        }

        buffer.removeAll(keepingCapacity: true)
        boundary = nil
        frameCount = 0
        fpsCounter = 0
        statusText = "正在连接 \(trimmed)..."

        let config = URLSessionConfiguration.default
        config.timeoutIntervalForRequest = 30
        config.timeoutIntervalForResource = 0
        config.waitsForConnectivity = false

        let session = URLSession(configuration: config, delegate: self, delegateQueue: nil)
        self.session = session
        let task = session.dataTask(with: url)
        self.task = task
        task.resume()

        fpsTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            guard let self else { return }
            DispatchQueue.main.async {
                self.frameCount = self.fpsCounter
                self.fpsCounter = 0
            }
        }
    }

    func disconnect() {
        fpsTimer?.invalidate()
        fpsTimer = nil
        task?.cancel()
        task = nil
        session?.invalidateAndCancel()
        session = nil
        buffer.removeAll(keepingCapacity: false)
        boundary = nil

        DispatchQueue.main.async {
            self.isConnected = false
            self.currentFrame = nil
            self.frameCount = 0
            self.statusText = "已断开"
        }
    }

    // MARK: - URLSessionDataDelegate

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
                    didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        guard let http = response as? HTTPURLResponse else {
            completionHandler(.cancel)
            DispatchQueue.main.async {
                self.statusText = "无效响应"
            }
            return
        }

        guard (200 ... 299).contains(http.statusCode) else {
            completionHandler(.cancel)
            DispatchQueue.main.async {
                self.statusText = "HTTP \(http.statusCode)"
            }
            return
        }

        if let contentType = http.value(forHTTPHeaderField: "Content-Type") {
            parseBoundary(from: contentType)
        }

        DispatchQueue.main.async {
            self.isConnected = true
            self.statusText = "已连接，等待视频帧..."
        }
        completionHandler(.allow)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        buffer.append(data)
        processBuffer()
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        DispatchQueue.main.async {
            self.isConnected = false
            if let error = error as NSError?, error.code != NSURLErrorCancelled {
                self.statusText = "连接失败: \(error.localizedDescription)"
            } else if self.statusText.hasPrefix("已连接") {
                self.statusText = "连接已关闭"
            }
        }
    }

    // MARK: - MJPEG parsing

    private func parseBoundary(from contentType: String) {
        let parts = contentType.split(separator: ";").map {
            $0.trimmingCharacters(in: .whitespaces)
        }
        for part in parts {
            let lower = part.lowercased()
            if lower.hasPrefix("boundary=") {
                let value = part.dropFirst("boundary=".count)
                let cleaned = value.trimmingCharacters(in: CharacterSet(charactersIn: "\"'"))
                boundary = Data("--\(cleaned)".utf8)
                return
            }
        }
    }

    private func processBuffer() {
        if boundary == nil {
            extractBoundaryFromBody()
        }
        guard let boundary else { return }

        while true {
            guard let range = buffer.range(of: boundary) else { return }

            if range.lowerBound > buffer.startIndex {
                let chunk = buffer[..<range.lowerBound]
                if let image = extractJPEG(from: chunk) {
                    publishFrame(image)
                }
            }

            buffer.removeSubrange(..<range.upperBound)

            if buffer.starts(with: Data("\r\n".utf8)) {
                buffer.removeSubrange(..<2)
            }

            if let headerEnd = buffer.range(of: Data("\r\n\r\n".utf8)) {
                buffer.removeSubrange(..<headerEnd.upperBound)
            } else {
                return
            }
        }
    }

    private func extractBoundaryFromBody() {
        guard let range = buffer.range(of: Data("\r\n\r\n".utf8)) else { return }
        let headerData = buffer[..<range.lowerBound]
        guard let headerText = String(data: headerData, encoding: .utf8) else { return }

        for line in headerText.split(separator: "\r\n") {
            let lower = line.lowercased()
            if lower.hasPrefix("content-type:"), lower.contains("boundary=") {
                parseBoundary(from: String(line.dropFirst("content-type:".count)))
                buffer.removeSubrange(..<range.upperBound)
                return
            }
        }
    }

    private func extractJPEG(from chunk: Data.SubSequence) -> PlatformImage? {
        let data = Data(chunk)
        guard let start = data.range(of: Data([0xFF, 0xD8])),
              let end = data.range(of: Data([0xFF, 0xD9]), in: start.lowerBound..<data.endIndex)
        else {
            return nil
        }
        let jpeg = data[start.lowerBound ... end.upperBound]
        return PlatformImage.fromJPEGData(jpeg)
    }

    private func publishFrame(_ image: PlatformImage) {
        DispatchQueue.main.async {
            self.currentFrame = image
            self.fpsCounter += 1
            if self.statusText.hasPrefix("已连接") {
                self.statusText = "直播中"
            }
        }
    }
}
