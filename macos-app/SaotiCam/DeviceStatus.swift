import Foundation
import SwiftUI

enum ModuleHealth: String, Equatable {
    case unknown
    case ok
    case warn
    case fail
    case offline

    var label: String {
        switch self {
        case .unknown: return "未知"
        case .ok: return "正常"
        case .warn: return "异常"
        case .fail: return "失败"
        case .offline: return "离线"
        }
    }

    var color: Color {
        switch self {
        case .unknown: return .secondary
        case .ok: return .green
        case .warn: return .orange
        case .fail: return .red
        case .offline: return .secondary
        }
    }

    var symbol: String {
        switch self {
        case .unknown: return "questionmark.circle"
        case .ok: return "checkmark.circle.fill"
        case .warn: return "exclamationmark.triangle.fill"
        case .fail: return "xmark.circle.fill"
        case .offline: return "circle.slash"
        }
    }
}

struct DeviceSnapshot: Equatable {
    var camera = ModuleHealth.unknown
    var display = ModuleHealth.unknown
    var cellular = ModuleHealth.unknown
    var wifi = ModuleHealth.unknown
    var usbStream = ModuleHealth.unknown
    var apn = "3gnet"
    var csq = -1
    var streamEnabled = false
    var mockCamera = false
    var mockDisplay = false
    var mockModem = false
    var updatedAt: Date?

    var overall: ModuleHealth {
        let parts = [camera, display, cellular]
        if parts.contains(.fail) { return .fail }
        if parts.contains(.warn) { return .warn }
        if parts.contains(.unknown) || parts.contains(.offline) {
            if camera == .ok { return .warn }
            return .unknown
        }
        return .ok
    }

    var signalBars: Int {
        // CSQ 0-31; 99 = unknown
        guard csq >= 0, csq < 99 else { return 0 }
        switch csq {
        case 0...7: return 1
        case 8...14: return 2
        case 15...21: return 3
        case 22...31: return 4
        default: return 0
        }
    }

    var signalText: String {
        guard csq >= 0 else { return "—" }
        if csq == 99 { return "无信号" }
        return "CSQ \(csq)"
    }
}

enum AppSection: String, CaseIterable, Identifiable, Hashable {
    case dashboard
    case live
    case cellular
    case camera
    case display
    case console

    var id: String { rawValue }

    var title: String {
        switch self {
        case .dashboard: return "总览"
        case .live: return "实时画面"
        case .cellular: return "蜂窝网络"
        case .camera: return "摄像头"
        case .display: return "显示屏"
        case .console: return "串口控制台"
        }
    }

    var symbol: String {
        switch self {
        case .dashboard: return "square.grid.2x2.fill"
        case .live: return "video.fill"
        case .cellular: return "antenna.radiowaves.left.and.right"
        case .camera: return "camera.fill"
        case .display: return "rectangle.portrait.on.rectangle.portrait.fill"
        case .console: return "terminal.fill"
        }
    }
}
