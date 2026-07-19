import Foundation

struct PendantStatus: Equatable {
    var cam = false
    var lcd = false
    var cell = false
    var wifi = false
    var batPct = -1
    var batV: Double?
    var charging = false
    var csq = -1
    var sleeping = false
    var busy = false
    var phase = "idle"
    var fw = ""
    var fwProto = 0
    var ble = false
    var hasAnswer = false
    var lastError = ""
    var apn = ""
    var model = ""
    var updatedAt: Date?

    var phaseLabel: String {
        switch phase {
        case "capturing": return "拍照中"
        case "uploading": return "解题中"
        case "result": return "已出答案"
        case "error": return "出错"
        default: return sleeping ? "休眠" : "就绪"
        }
    }

    var signalBars: Int {
        guard csq >= 0, csq < 99 else { return 0 }
        switch csq {
        case 0...7: return 1
        case 8...14: return 2
        case 15...21: return 3
        case 22...31: return 4
        default: return 0
        }
    }

    static func parse(_ data: Data) -> PendantStatus? {
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return nil
        }
        var s = PendantStatus()
        s.cam = obj["cam"] as? Bool ?? false
        s.lcd = obj["lcd"] as? Bool ?? false
        s.cell = obj["cell"] as? Bool ?? false
        s.wifi = obj["wifi"] as? Bool ?? false
        s.batPct = obj["bat_pct"] as? Int ?? -1
        if let v = obj["bat_v"] as? Double { s.batV = v }
        if let v = obj["bat_v"] as? NSNumber { s.batV = v.doubleValue }
        s.charging = obj["charging"] as? Bool ?? false
        s.csq = obj["csq"] as? Int ?? -1
        s.sleeping = obj["sleeping"] as? Bool ?? false
        s.busy = obj["busy"] as? Bool ?? false
        s.phase = obj["phase"] as? String ?? "idle"
        s.fw = obj["fw"] as? String ?? ""
        s.fwProto = obj["fw_proto"] as? Int ?? 0
        s.ble = obj["ble"] as? Bool ?? false
        s.hasAnswer = obj["has_answer"] as? Bool ?? false
        s.lastError = obj["last_error"] as? String ?? ""
        s.apn = obj["apn"] as? String ?? ""
        s.model = obj["model"] as? String ?? ""
        s.updatedAt = Date()
        return s
    }
}

struct AnswerItem: Identifiable, Equatable, Codable {
    let id: UUID
    let text: String
    let date: Date

    init(text: String, date: Date = Date()) {
        self.id = UUID()
        self.text = text
        self.date = date
    }
}

final class AnswerStore: ObservableObject {
    @Published private(set) var items: [AnswerItem] = []
    private let key = "saoti.answer.history"
    private let maxCount = 30

    init() { load() }

    func push(_ text: String) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        items.insert(AnswerItem(text: trimmed), at: 0)
        if items.count > maxCount { items = Array(items.prefix(maxCount)) }
        save()
    }

    var latest: AnswerItem? { items.first }

    private func save() {
        if let data = try? JSONEncoder().encode(items) {
            UserDefaults.standard.set(data, forKey: key)
        }
    }

    private func load() {
        guard let data = UserDefaults.standard.data(forKey: key),
              let decoded = try? JSONDecoder().decode([AnswerItem].self, from: data) else { return }
        items = decoded
    }
}
