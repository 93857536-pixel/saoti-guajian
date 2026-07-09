import SwiftUI

enum ConnectionMode: String, CaseIterable, Identifiable {
    case usb = "USB 串口"
    case wifi = "WiFi"

    var id: String { rawValue }
}

struct RootView: View {
    @StateObject private var hub = DeviceHub()
    @StateObject private var usbClient = SerialStreamClient()
    @StateObject private var wifiClient = MjpegStreamClient()

    @State private var section: AppSection = .dashboard
    @State private var liveMode: ConnectionMode = .usb
    @State private var liveHost = "192.168.4.1"
    @State private var showLiveControls = true

    var body: some View {
        NavigationSplitView {
            List(selection: $section) {
                Section("设备") {
                    ForEach(AppSection.allCases) { item in
                        Label(item.title, systemImage: item.symbol)
                            .tag(item)
                    }
                }
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 180, ideal: 210, max: 260)
            .safeAreaInset(edge: .bottom) {
                connectionBar
            }
        } detail: {
            detail
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .navigationTitle(section.title)
        .toolbar {
            ToolbarItemGroup(placement: .automatic) {
                Picker("波特率", selection: $hub.baudChoice) {
                    Text("115200 诊断").tag(DeviceHub.BaudChoice.diag)
                    Text("921600 推流").tag(DeviceHub.BaudChoice.stream)
                }
                .pickerStyle(.menu)
                .disabled(hub.isConnected)

                Button {
                    hub.refreshAndConnectIfPossible()
                } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .help("刷新串口并尝试连接")

                if hub.isConnected {
                    Button("断开") { hub.disconnect() }
                } else {
                    Button("连接") { hub.connect() }
                        .keyboardShortcut(.return, modifiers: [])
                }
            }
        }
        .onAppear {
            hub.baudChoice = .diag
            hub.refreshAndConnectIfPossible()
        }
    }

    @ViewBuilder
    private var detail: some View {
        switch section {
        case .dashboard:
            DashboardView(hub: hub) { section = $0 }
        case .live:
            liveView
        case .cellular:
            CellularConfigView(hub: hub)
        case .camera:
            CameraModuleView(hub: hub, usbClient: usbClient, onOpenLive: { section = .live })
        case .display:
            DisplayModuleView(hub: hub)
        case .console:
            ConsoleView(hub: hub)
        }
    }

    private var connectionBar: some View {
        VStack(alignment: .leading, spacing: 8) {
            Divider()
            Picker("串口", selection: $hub.selectedPort) {
                if hub.availablePorts.isEmpty {
                    Text("未检测到串口").tag("")
                }
                ForEach(hub.availablePorts, id: \.self) { port in
                    Text(port.replacingOccurrences(of: "/dev/", with: "")).tag(port)
                }
            }
            .disabled(hub.isConnected)

            HStack(spacing: 6) {
                Circle()
                    .fill(hub.isConnected ? Color.green : Color.orange)
                    .frame(width: 8, height: 8)
                Text(hub.statusText)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
                Spacer()
                if !hub.isConnected {
                    Button("连接") { hub.refreshAndConnectIfPossible() }
                        .buttonStyle(.borderedProminent)
                        .controlSize(.small)
                }
            }
        }
        .padding(12)
    }

    private var liveView: some View {
        ZStack {
            Color.black
            if liveConnected {
                CameraStreamView(
                    cgImage: liveCGImage,
                    debugInfo: liveMode == .usb ? usbClient.frameDebugInfo : wifiDebug
                )
                .id(liveMode == .usb ? usbClient.frameSequence : wifiClient.frameCount)
                .onTapGesture {
                    withAnimation { showLiveControls.toggle() }
                }
            } else {
                VStack(spacing: 12) {
                    Image(systemName: "video.slash")
                        .font(.system(size: 40))
                        .foregroundStyle(.secondary)
                    Text("未连接实时流")
                        .foregroundStyle(.secondary)
                    Text("诊断固件用 115200；推流固件用 921600")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            if showLiveControls {
                VStack {
                    Spacer()
                    liveControls
                }
                .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
    }

    private var liveControls: some View {
        VStack(spacing: 10) {
            Picker("模式", selection: $liveMode) {
                ForEach(ConnectionMode.allCases) { mode in
                    Text(mode.rawValue).tag(mode)
                }
            }
            .pickerStyle(.segmented)
            .disabled(liveConnected)

            if liveMode == .usb {
                Text("使用推流固件 (nolcd) + 921600，与诊断页串口互斥")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } else {
                TextField("设备 IP", text: $liveHost)
                    .textFieldStyle(.roundedBorder)
                    .disabled(liveConnected)
            }

            HStack {
                Button(liveConnected ? "断开画面" : "开始预览") {
                    if liveConnected {
                        disconnectLive()
                    } else {
                        connectLive()
                    }
                }
                .buttonStyle(.borderedProminent)
                Spacer()
                Text(liveStatus)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .padding()
        .background(.regularMaterial)
    }

    private var liveConnected: Bool {
        liveMode == .usb ? usbClient.isConnected : wifiClient.isConnected
    }

    private var liveCGImage: CGImage? {
        if liveMode == .usb { return usbClient.displayCGImage }
        #if canImport(AppKit)
        return wifiClient.currentFrame?.cgImage(forProposedRect: nil, context: nil, hints: nil)
        #else
        return nil
        #endif
    }

    private var wifiDebug: String {
        guard let frame = wifiClient.currentFrame else { return "" }
        #if canImport(AppKit)
        return "\(Int(frame.size.width))×\(Int(frame.size.height)) WiFi"
        #else
        return "WiFi"
        #endif
    }

    private var liveStatus: String {
        liveMode == .usb ? usbClient.statusText : wifiClient.statusText
    }

    private func connectLive() {
        hub.disconnect()
        switch liveMode {
        case .usb:
            let port = hub.selectedPort.isEmpty ? (hub.availablePorts.first ?? "") : hub.selectedPort
            usbClient.connect(portPath: port)
        case .wifi:
            wifiClient.connect(host: liveHost)
        }
    }

    private func disconnectLive() {
        usbClient.disconnect()
        wifiClient.disconnect()
    }
}
