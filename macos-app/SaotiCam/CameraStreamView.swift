import SwiftUI

struct CameraStreamView: View {
    let cgImage: CGImage?
    let debugInfo: String

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Color.black
                #if canImport(AppKit)
                FrameDisplayView(cgImage: cgImage)
                    .frame(width: geometry.size.width, height: geometry.size.height)
                #else
                if let cgImage {
                    Image(decorative: cgImage, scale: 1.0)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                } else {
                    ProgressView("等待画面...")
                        .foregroundStyle(.white)
                }
                #endif

                if !debugInfo.isEmpty {
                    VStack {
                        HStack {
                            Text(debugInfo)
                                .font(.caption.monospaced())
                                .padding(6)
                                .background(.black.opacity(0.55))
                                .foregroundStyle(.white)
                            Spacer()
                        }
                        Spacer()
                    }
                    .padding(8)
                }
            }
        }
    }
}

#Preview {
    CameraStreamView(cgImage: nil, debugInfo: "")
        .frame(width: 640, height: 480)
        .background(Color.black)
}
