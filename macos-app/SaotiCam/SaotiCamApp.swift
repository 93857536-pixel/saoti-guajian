import SwiftUI

@main
struct SaotiCamApp: App {
    var body: some Scene {
        WindowGroup {
            RootView()
                .frame(minWidth: 980, minHeight: 640)
        }
        .defaultSize(width: 1180, height: 760)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
