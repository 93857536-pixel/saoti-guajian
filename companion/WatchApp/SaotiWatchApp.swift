import SwiftUI

@main
struct SaotiWatchApp: App {
    @StateObject private var proxy = WatchPhoneProxy()

    var body: some Scene {
        WindowGroup {
            WatchContentView()
                .environmentObject(proxy)
        }
    }
}

struct WatchContentView: View {
    @EnvironmentObject private var proxy: WatchPhoneProxy

    var body: some View {
        NavigationStack {
            VStack(spacing: 10) {
                Text(phaseLabel)
                    .font(.headline)
                HStack {
                    Text("电 \(max(proxy.batPct, 0))%")
                    Text("信号 \(proxy.csq >= 0 ? "\(proxy.csq)" : "—")")
                }
                .font(.caption2)
                .foregroundStyle(.secondary)

                Button {
                    proxy.send("scan")
                } label: {
                    Label(proxy.busy ? "进行中" : "扫题", systemImage: "viewfinder")
                }
                .disabled(proxy.busy || !proxy.connected)

                Button("唤醒") { proxy.send("wake") }
                    .font(.caption)

                if !proxy.answer.isEmpty {
                    ScrollView {
                        Text(proxy.answer)
                            .font(.caption2)
                    }
                }
            }
            .padding(.horizontal, 6)
            .navigationTitle("扫题")
        }
    }

    private var phaseLabel: String {
        if !proxy.connected { return "手机未连挂件" }
        switch proxy.phase {
        case "capturing": return "拍照中"
        case "uploading": return "解题中"
        case "result": return "已出答案"
        default: return proxy.sleeping ? "休眠" : "就绪"
        }
    }
}
