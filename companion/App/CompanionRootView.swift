import SwiftUI

#if canImport(UIKit)
import UIKit
typealias PlatformImage = UIImage
#elseif canImport(AppKit)
import AppKit
typealias PlatformImage = NSImage
#endif

struct CompanionRootView: View {
    @EnvironmentObject private var ble: BleClient
    @State private var tab = 3 // 默认「设备」
    @AppStorage("saoti.seenBleHint") private var seenBleHint = false
    @State private var showHint = false

    var body: some View {
        TabView(selection: $tab) {
            CompanionDashboard()
                .tabItem { Label("控制", systemImage: "antenna.radiowaves.left.and.right") }
                .tag(0)
            CompanionPreviewView()
                .tabItem { Label("取景", systemImage: "camera.viewfinder") }
                .tag(1)
            CompanionAnswerView()
                .tabItem { Label("答案", systemImage: "doc.text") }
                .tag(2)
            CompanionDevicesView()
                .tabItem { Label("设备", systemImage: "link") }
                .tag(3)
        }
        #if os(macOS)
        .frame(minWidth: 720, minHeight: 520)
        #endif
        .onAppear {
            if !seenBleHint {
                showHint = true
            }
            if ble.state != .ready {
                ble.startScan(autoConnect: true)
            }
        }
        .alert("不要用系统蓝牙设置搜挂件", isPresented: $showHint) {
            Button("知道了，去设备页") {
                seenBleHint = true
                tab = 3
                ble.startScan(autoConnect: true)
            }
        } message: {
            Text("iPhone/iPad 的「设置 → 蓝牙」永远不会显示本挂件（不是耳机那种设备）。\n\n请看挂件屏幕上的名字（如 Saoti-F79D），在本 App「设备」页扫描连接。")
        }
    }
}

struct CompanionDashboard: View {
    @EnvironmentObject private var ble: BleClient

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    if ble.state != .ready {
                        Button {
                            // parent tab switch can't from here easily — scan anyway
                            ble.startScan(autoConnect: true)
                        } label: {
                            Label("未连接：点此扫描（勿用系统设置）", systemImage: "exclamationmark.triangle.fill")
                                .frame(maxWidth: .infinity)
                                .padding()
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(.orange)
                    }
                    statusCard
                    actionRow
                    metaGrid
                }
                .padding()
            }
            .navigationTitle("扫题挂件")
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Text(ble.state.rawValue)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
    }

    private var statusCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text(ble.status.phaseLabel)
                    .font(.title2.weight(.bold))
                Spacer()
                if ble.status.charging {
                    Label("充电中", systemImage: "bolt.fill")
                        .foregroundStyle(.orange)
                }
            }
            HStack(spacing: 16) {
                Label("\(max(ble.status.batPct, 0))%", systemImage: "battery.100")
                Label(ble.status.csq >= 0 ? "CSQ \(ble.status.csq)" : "无信号",
                      systemImage: "antenna.radiowaves.left.and.right")
                if ble.status.sleeping {
                    Label("休眠", systemImage: "moon.fill")
                }
            }
            .font(.subheadline)
            .foregroundStyle(.secondary)
            if !ble.status.lastError.isEmpty {
                Text(ble.status.lastError)
                    .font(.caption)
                    .foregroundStyle(.red)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }

    private var actionRow: some View {
        VStack(spacing: 12) {
            Button {
                ble.scanQuestion()
            } label: {
                Label(ble.status.busy ? "扫题进行中…" : "扫题", systemImage: "viewfinder")
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)
            }
            .buttonStyle(.borderedProminent)
            .disabled(ble.state != .ready || ble.status.busy)

            HStack {
                Button("唤醒") { ble.wake() }
                    .buttonStyle(.bordered)
                Button("补光开") { ble.flash(true) }
                    .buttonStyle(.bordered)
                Button("补光关") { ble.flash(false) }
                    .buttonStyle(.bordered)
                Button("刷新状态") { ble.sendCommand("status") }
                    .buttonStyle(.bordered)
            }
            .disabled(ble.state != .ready)
        }
    }

    private var metaGrid: some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 140))], spacing: 10) {
            meta("摄像头", ble.status.cam ? "就绪" : "离线")
            meta("屏幕", ble.status.lcd ? "就绪" : "离线")
            meta("4G", ble.status.cell ? "就绪" : "离线")
            meta("固件", ble.status.fw.isEmpty ? "—" : ble.status.fw)
            meta("模型", ble.status.model.isEmpty ? "—" : ble.status.model)
            meta("APN", ble.status.apn.isEmpty ? "—" : ble.status.apn)
        }
    }

    private func meta(_ title: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.headline)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.secondary.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
    }
}

struct CompanionPreviewView: View {
    @EnvironmentObject private var ble: BleClient

    var body: some View {
        NavigationStack {
            VStack(spacing: 16) {
                if let data = ble.thumbData, let img = platformImage(from: data) {
                    #if os(iOS)
                    Image(uiImage: img)
                        .resizable()
                        .scaledToFit()
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                    #else
                    Image(nsImage: img)
                        .resizable()
                        .scaledToFit()
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                    #endif
                } else {
                    VStack(spacing: 8) {
                        Image(systemName: "camera.viewfinder")
                            .font(.system(size: 48))
                            .foregroundStyle(.secondary)
                        Text("暂无取景").font(.headline)
                        Text("连接后点「刷新取景」")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxHeight: .infinity)
                }
                Text("此为 BLE 预览（约 320×240），不是扫题原图。要更清晰请用 Mac 上 SaotiCam USB 推流，或直接按键扫题。")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                if ble.state != .ready {
                    Text("未连接挂件：请到「设备」页扫描连接 Saoti-XXXX")
                        .font(.caption)
                        .foregroundStyle(.red)
                        .multilineTextAlignment(.center)
                }
                Button("刷新取景") { ble.requestThumb() }
                    .buttonStyle(.borderedProminent)
                    .disabled(ble.state != .ready)
            }
            .padding()
            .navigationTitle("取景")
        }
    }

    private func platformImage(from data: Data) -> PlatformImage? {
        #if os(iOS)
        return UIImage(data: data)
        #else
        return NSImage(data: data)
        #endif
    }
}

struct CompanionAnswerView: View {
    @EnvironmentObject private var ble: BleClient

    var body: some View {
        NavigationStack {
            List {
                if !ble.answerText.isEmpty {
                    Section("当前答案") {
                        Text(ble.answerText)
                            .textSelection(.enabled)
                    }
                }
                Section("历史") {
                    ForEach(ble.answers.items) { item in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(item.text.prefix(200) + (item.text.count > 200 ? "…" : ""))
                            Text(item.date.formatted(date: .abbreviated, time: .shortened))
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                        .onTapGesture { ble.answerText = item.text }
                    }
                }
            }
            .navigationTitle("答案")
            .toolbar {
                Button("拉取") { ble.requestAnswer() }
                    .disabled(ble.state != .ready)
            }
        }
    }
}

struct CompanionDevicesView: View {
    @EnvironmentObject private var ble: BleClient

    var body: some View {
        NavigationStack {
            List {
                Section {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("正确连接方式")
                            .font(.headline)
                        Text("1. 打开手机系统蓝牙开关（只开开关）\n2. 看挂件屏幕名字，如 Saoti-F79D\n3. 本页点「扫描」并点选设备\n\n注意：不要在「设置→蓝牙」里搜——那里永远没有挂件。若已连接过，列表可能直接显示已连接设备。")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.vertical, 4)
                }
                Section("附近设备") {
                    if ble.devices.isEmpty {
                        Text(ble.state == .scanning
                              ? "扫描中…请靠近挂件，对照屏幕名字"
                              : ble.state == .unauthorized
                                ? "无蓝牙权限：系统设置 → 扫题挂件 → 蓝牙"
                                : "尚未发现。点右上角「扫描」")
                            .foregroundStyle(.secondary)
                    }
                    ForEach(ble.devices) { d in
                        Button {
                            ble.connect(d)
                        } label: {
                            HStack {
                                VStack(alignment: .leading) {
                                    Text(d.name).font(.headline)
                                    Text("点按连接 · \(d.id.uuidString.prefix(8))")
                                        .font(.caption2)
                                        .foregroundStyle(.secondary)
                                }
                                Spacer()
                                Text("\(d.rssi) dBm")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
                Section("状态") {
                    LabeledContent("连接", value: ble.state.rawValue)
                    if !ble.lastEvent.isEmpty {
                        Text(ble.lastEvent).font(.caption.monospaced())
                    }
                }
                Section("日志") {
                    ForEach(Array(ble.console.suffix(40).enumerated()), id: \.offset) { _, line in
                        Text(line).font(.caption.monospaced())
                    }
                }
            }
            .navigationTitle("设备")
            .toolbar {
                if ble.state == .ready || ble.state == .connected {
                    Button("断开") { ble.disconnect() }
                } else {
                    Button("扫描") { ble.startScan(autoConnect: true) }
                }
            }
            .onAppear {
                if ble.state != .ready && ble.state != .connecting {
                    ble.startScan(autoConnect: true)
                }
            }
        }
    }
}
