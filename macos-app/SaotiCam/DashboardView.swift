import SwiftUI

struct DashboardView: View {
    @ObservedObject var hub: DeviceHub
    let onOpen: (AppSection) -> Void

    private let columns = [
        GridItem(.adaptive(minimum: 180, maximum: 260), spacing: 14)
    ]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                hero

                SectionHeader(title: "模块状态", subtitle: "每 3 秒自动刷新，也可手动同步")

                LazyVGrid(columns: columns, spacing: 14) {
                    ModuleCard(
                        title: "摄像头",
                        subtitle: hub.snapshot.mockCamera ? "Mock 模式" : "OV5640",
                        health: hub.snapshot.camera,
                        symbol: "camera.fill",
                        detail: hub.snapshot.camera == .ok ? "SCCB 已识别" : "检查排线 / 3.3V"
                    )
                    .onTapGesture { onOpen(.camera) }

                    ModuleCard(
                        title: "显示屏",
                        subtitle: hub.snapshot.mockDisplay ? "Mock 串口 UI" : "ST7789",
                        health: hub.snapshot.display,
                        symbol: "rectangle.portrait.on.rectangle.portrait.fill"
                    )
                    .onTapGesture { onOpen(.display) }

                    ModuleCard(
                        title: "蜂窝 4G",
                        subtitle: hub.snapshot.apn,
                        health: hub.snapshot.cellular,
                        symbol: "antenna.radiowaves.left.and.right",
                        detail: hub.snapshot.signalText
                    )
                    .onTapGesture { onOpen(.cellular) }

                    ModuleCard(
                        title: "Wi‑Fi",
                        subtitle: hub.snapshot.wifi == .ok ? "已连接" : "未配置 / 离线",
                        health: hub.snapshot.wifi,
                        symbol: "wifi"
                    )

                    ModuleCard(
                        title: "USB 推流",
                        subtitle: hub.snapshot.streamEnabled ? "固件已启用" : "当前固件未开流",
                        health: hub.snapshot.usbStream,
                        symbol: "cable.connector"
                    )
                    .onTapGesture { onOpen(.live) }
                }

                quickActions
            }
            .padding(24)
        }
        .background(pageBackground)
    }

    private var hero: some View {
        GlassPanel {
            HStack(alignment: .center, spacing: 18) {
                ZStack {
                    Circle()
                        .fill(
                            LinearGradient(
                                colors: [
                                    Color.accentColor.opacity(0.85),
                                    Color.cyan.opacity(0.55)
                                ],
                                startPoint: .topLeading,
                                endPoint: .bottomTrailing
                            )
                        )
                        .frame(width: 64, height: 64)
                    Image(systemName: "cpu")
                        .font(.system(size: 28, weight: .semibold))
                        .foregroundStyle(.white)
                }

                VStack(alignment: .leading, spacing: 6) {
                    Text("扫题挂件")
                        .font(.largeTitle.weight(.bold))
                    Text(hub.isConnected ? hub.statusText : "连接 USB 串口后即可实时检测模块")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    HStack(spacing: 8) {
                        Label(hub.snapshot.overall.label, systemImage: hub.snapshot.overall.symbol)
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(hub.snapshot.overall.color)
                        if let updated = hub.snapshot.updatedAt {
                            Text("· 更新于 \(updated.formatted(date: .omitted, time: .standard))")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }

                Spacer()

                VStack(spacing: 10) {
                    Button {
                        hub.requestStatus()
                    } label: {
                        Label("刷新状态", systemImage: "arrow.clockwise")
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(!hub.isConnected)

                    Button {
                        onOpen(.cellular)
                    } label: {
                        Label("配置 4G", systemImage: "slider.horizontal.3")
                    }
                    .buttonStyle(.bordered)
                    .disabled(!hub.isConnected)
                }
            }
        }
    }

    private var quickActions: some View {
        VStack(alignment: .leading, spacing: 12) {
            SectionHeader(title: "快捷操作")
            HStack(spacing: 12) {
                actionButton("实时画面", "video") { onOpen(.live) }
                actionButton("4G 诊断", "stethoscope") { hub.runCellularDiag() }
                actionButton("UART 扫描", "point.3.connected.trianglepath.dotted") { hub.runUARTScan() }
                actionButton("控制台", "terminal") { onOpen(.console) }
            }
        }
    }

    private func actionButton(_ title: String, _ symbol: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Label(title, systemImage: symbol)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
        }
        .buttonStyle(.bordered)
        .disabled(!hub.isConnected)
    }

    private var pageBackground: some View {
        LinearGradient(
            colors: [
                Color(nsColor: .windowBackgroundColor),
                Color.accentColor.opacity(0.05)
            ],
            startPoint: .top,
            endPoint: .bottom
        )
        .ignoresSafeArea()
    }
}
