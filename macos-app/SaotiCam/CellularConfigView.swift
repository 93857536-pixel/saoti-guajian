import SwiftUI

struct CellularConfigView: View {
    @ObservedObject var hub: DeviceHub

    private let presets = ["3gnet", "cmnet", "cmiot", "uninet", "wonet", "ctnet"]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                SectionHeader(
                    title: "蜂窝网络",
                    subtitle: "蜗牛移动默认 APN 为 3gnet（联通转售），可按卡商说明修改"
                )

                GlassPanel {
                    HStack(spacing: 20) {
                        signalMeter
                        VStack(alignment: .leading, spacing: 8) {
                            Text(hub.snapshot.cellular.label)
                                .font(.title3.weight(.semibold))
                                .foregroundStyle(hub.snapshot.cellular.color)
                            Text("当前 APN  \(hub.snapshot.apn)")
                                .font(.body.monospaced())
                            Text(hub.snapshot.signalText)
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                            Text("红灯常亮仅表示模块上电；需 AT 通后才能读 SIM / 信号")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                        Spacer()
                    }
                }

                GlassPanel {
                    VStack(alignment: .leading, spacing: 14) {
                        Text("APN 配置")
                            .font(.headline)
                        TextField("例如 3gnet", text: $hub.apnDraft)
                            .textFieldStyle(.roundedBorder)
                            .font(.body.monospaced())

                        HStack(spacing: 8) {
                            ForEach(presets, id: \.self) { item in
                                Button(item) {
                                    hub.apnDraft = item
                                }
                                .buttonStyle(.bordered)
                                .controlSize(.small)
                            }
                        }

                        HStack {
                            Button {
                                hub.applyAPN(hub.apnDraft)
                            } label: {
                                Label("写入设备", systemImage: "square.and.arrow.down")
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(!hub.isConnected || hub.apnDraft.trimmingCharacters(in: .whitespaces).isEmpty)

                            Button {
                                hub.runNetAttach()
                            } label: {
                                Label("附着网络", systemImage: "network")
                            }
                            .buttonStyle(.bordered)
                            .disabled(!hub.isConnected)

                            Button {
                                hub.runCellularDiag()
                            } label: {
                                Label("完整诊断", systemImage: "stethoscope")
                            }
                            .buttonStyle(.bordered)
                            .disabled(!hub.isConnected)
                        }
                    }
                }

                GlassPanel {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("接线核对")
                            .font(.headline)
                        bullet("ESP GPIO21 → 模块 RX")
                        bullet("ESP GPIO47 ← 模块 TX（交叉）")
                        bullet("模块 VIN = 5V，PEN = 3.3V，与 ESP 共地")
                        bullet("不要占用 GPIO43/44（电脑 USB 串口）")
                        Button {
                            hub.runUARTScan()
                        } label: {
                            Label("全端口扫描 UART", systemImage: "point.3.connected.trianglepath.dotted")
                        }
                        .buttonStyle(.bordered)
                        .disabled(!hub.isConnected)
                    }
                }
            }
            .padding(24)
        }
    }

    private var signalMeter: some View {
        HStack(alignment: .bottom, spacing: 5) {
            ForEach(1...4, id: \.self) { bar in
                RoundedRectangle(cornerRadius: 3, style: .continuous)
                    .fill(bar <= hub.snapshot.signalBars ? Color.accentColor : Color.secondary.opacity(0.2))
                    .frame(width: 10, height: CGFloat(12 + bar * 10))
            }
        }
        .frame(width: 64, height: 64)
        .padding(12)
        .background(Color.accentColor.opacity(0.08), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
    }

    private func bullet(_ text: String) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: "checkmark.circle")
                .foregroundStyle(.secondary)
            Text(text)
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
    }
}
