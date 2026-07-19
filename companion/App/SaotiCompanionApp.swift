import SwiftUI

@main
struct SaotiCompanionApp: App {
    @StateObject private var ble = BleClient()

    var body: some Scene {
        WindowGroup {
            CompanionRootView()
                .environmentObject(ble)
                #if os(iOS)
                .onAppear { WatchBridge.shared.attach(ble) }
                .onReceive(ble.$status) { new in
                    WatchBridge.shared.pushStatus(new, answer: ble.answerText)
                }
                .onReceive(ble.$answerText) { new in
                    WatchBridge.shared.pushStatus(ble.status, answer: new)
                }
                #endif
        }
        #if os(macOS)
        .defaultSize(width: 980, height: 720)
        #endif
    }
}
