import SwiftUI

/// 保留旧入口，实际 UI 由 RootView 承载。
struct ContentView: View {
    var body: some View {
        RootView()
    }
}

#Preview {
    ContentView()
        .frame(width: 1100, height: 720)
}
