import Foundation

#if os(iOS)
import WatchConnectivity

/// iPhone ↔ Apple Watch：转发扫题命令与状态摘要
final class WatchBridge: NSObject, ObservableObject, WCSessionDelegate {
    static let shared = WatchBridge()

    private weak var ble: BleClient?

    override init() {
        super.init()
        guard WCSession.isSupported() else { return }
        WCSession.default.delegate = self
        WCSession.default.activate()
    }

    func attach(_ ble: BleClient) {
        self.ble = ble
    }

    @MainActor
    func pushStatus(_ status: PendantStatus, answer: String) {
        guard WCSession.default.activationState == .activated,
              WCSession.default.isPaired,
              WCSession.default.isWatchAppInstalled else { return }
        var ctx: [String: Any] = [
            "phase": status.phase,
            "bat_pct": status.batPct,
            "csq": status.csq,
            "busy": status.busy,
            "sleeping": status.sleeping,
            "connected": ble?.state == .ready,
            "last_error": status.lastError
        ]
        if !answer.isEmpty {
            ctx["answer"] = String(answer.prefix(400))
        }
        try? WCSession.default.updateApplicationContext(ctx)
    }

    nonisolated func session(
        _ session: WCSession,
        activationDidCompleteWith activationState: WCSessionActivationState,
        error: Error?
    ) {}

    nonisolated func sessionDidBecomeInactive(_ session: WCSession) {}
    nonisolated func sessionDidDeactivate(_ session: WCSession) {
        session.activate()
    }

    nonisolated func session(_ session: WCSession, didReceiveMessage message: [String: Any]) {
        Task { @MainActor in
            self.handleWatchCommand(message)
        }
    }

    nonisolated func session(_ session: WCSession, didReceiveUserInfo userInfo: [String: Any]) {
        Task { @MainActor in
            self.handleWatchCommand(userInfo)
        }
    }

    @MainActor
    private func handleWatchCommand(_ message: [String: Any]) {
        guard let cmd = message["cmd"] as? String else { return }
        switch cmd {
        case "scan": self.ble?.scanQuestion()
        case "wake": self.ble?.wake()
        case "status": self.ble?.sendCommand("status")
        case "answer": self.ble?.requestAnswer()
        default: break
        }
    }
}
#endif

#if os(watchOS)
import WatchConnectivity

final class WatchPhoneProxy: NSObject, ObservableObject, WCSessionDelegate {
    @Published var phase = "idle"
    @Published var batPct = -1
    @Published var csq = -1
    @Published var busy = false
    @Published var sleeping = false
    @Published var connected = false
    @Published var lastError = ""
    @Published var answer = ""

    override init() {
        super.init()
        if WCSession.isSupported() {
            WCSession.default.delegate = self
            WCSession.default.activate()
        }
    }

    func send(_ cmd: String) {
        guard WCSession.default.isReachable else {
            WCSession.default.transferUserInfo(["cmd": cmd])
            return
        }
        WCSession.default.sendMessage(["cmd": cmd], replyHandler: nil) { err in
            print("Watch send fail: \(err.localizedDescription)")
        }
    }

    nonisolated func session(
        _ session: WCSession,
        activationDidCompleteWith activationState: WCSessionActivationState,
        error: Error?
    ) {}

    nonisolated func session(
        _ session: WCSession,
        didReceiveApplicationContext applicationContext: [String: Any]
    ) {
        Task { @MainActor in
            self.phase = applicationContext["phase"] as? String ?? "idle"
            self.batPct = applicationContext["bat_pct"] as? Int ?? -1
            self.csq = applicationContext["csq"] as? Int ?? -1
            self.busy = applicationContext["busy"] as? Bool ?? false
            self.sleeping = applicationContext["sleeping"] as? Bool ?? false
            self.connected = applicationContext["connected"] as? Bool ?? false
            self.lastError = applicationContext["last_error"] as? String ?? ""
            if let a = applicationContext["answer"] as? String { self.answer = a }
        }
    }
}
#endif
