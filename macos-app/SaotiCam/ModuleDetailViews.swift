import SwiftUI

struct ConsoleView: View {
    @ObservedObject var hub: DeviceHub
    @State private var command = ""

    var body: some View {
        VStack(spacing: 0) {
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 4) {
                        ForEach(Array(hub.consoleLines.enumerated()), id: \.offset) { index, line in
                            Text(line)
                                .font(.system(.caption, design: .monospaced))
                                .foregroundStyle(line.hasPrefix(">") ? Color.accentColor : Color.primary)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(index)
                        }
                    }
                    .padding(16)
                }
                .background(Color(nsColor: .textBackgroundColor))
                .onChange(of: hub.consoleLines.count) { _ in
                    if let last = hub.consoleLines.indices.last {
                        withAnimation(.easeOut(duration: 0.15)) {
                            proxy.scrollTo(last, anchor: .bottom)
                        }
                    }
                }
            }

            Divider()

            HStack(spacing: 10) {
                TextField("发送命令，例如 ? / APN=3gnet / DIAG", text: $command)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit(send)
                Button("发送", action: send)
                    .buttonStyle(.borderedProminent)
                    .disabled(!hub.isConnected || command.trimmingCharacters(in: .whitespaces).isEmpty)
                Button("?") { hub.requestStatus() }
                    .disabled(!hub.isConnected)
            }
            .padding(12)
        }
    }

    private func send() {
        let text = command.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        hub.sendRaw(text)
        command = ""
    }
}

struct CameraModuleView: View {
    @ObservedObject var hub: DeviceHub
    @ObservedObject var usbClient: SerialStreamClient
    var onOpenLive: () -> Void = {}

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                SectionHeader(title: "摄像头", subtitle: "OV5640 状态与实时预览入口")
                GlassPanel {
                    VStack(alignment: .leading, spacing: 10) {
                        Label(hub.snapshot.camera.label, systemImage: hub.snapshot.camera.symbol)
                            .foregroundStyle(hub.snapshot.camera.color)
                            .font(.title3.weight(.semibold))
                        Text(hub.snapshot.mockCamera ? "当前为 Mock 摄像头" : "硬件 OV5640")
                            .foregroundStyle(.secondary)
                        Text("若状态正常但仍黑屏，多为硬件 JPEG 损坏或镜头/排线问题。")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        HStack {
                            Button("刷新状态") { hub.requestStatus() }
                                .buttonStyle(.bordered)
                            Button("打开实时画面", action: onOpenLive)
                                .buttonStyle(.borderedProminent)
                        }
                    }
                }

                if usbClient.isConnected, let image = usbClient.displayCGImage {
                    GlassPanel {
                        VStack(alignment: .leading, spacing: 10) {
                            Text("当前预览")
                                .font(.headline)
                            FrameDisplayView(cgImage: image)
                                .frame(height: 280)
                                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                            Text(usbClient.statusText)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                } else {
                    GlassPanel {
                        Text("实时预览请到「实时画面」页，使用 921600 推流固件连接。")
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .padding(24)
        }
    }
}

struct DisplayModuleView: View {
    @ObservedObject var hub: DeviceHub

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                SectionHeader(title: "显示屏", subtitle: "ST7789 240×240")
                GlassPanel {
                    VStack(alignment: .leading, spacing: 8) {
                        Label(hub.snapshot.display.label, systemImage: hub.snapshot.display.symbol)
                            .foregroundStyle(hub.snapshot.display.color)
                            .font(.title3.weight(.semibold))
                        Text(hub.snapshot.mockDisplay ? "Mock 显示：状态走串口日志" : "真实 ST7789 SPI")
                            .foregroundStyle(.secondary)
                        Text("引脚 MOSI35 / SCLK36 / CS37 / DC38 / RST39 / BL40")
                            .font(.caption.monospaced())
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .padding(24)
        }
    }
}
