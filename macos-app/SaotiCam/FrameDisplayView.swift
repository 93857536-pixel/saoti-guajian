import SwiftUI

#if canImport(AppKit)
import AppKit

final class FrameCGView: NSView {
    var cgImage: CGImage? {
        didSet { needsDisplay = true }
    }

    override var isOpaque: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.setFillColor(NSColor.black.cgColor)
        ctx.fill(bounds)

        guard let image = cgImage else { return }
        let dest = Self.aspectFit(
            imageWidth: CGFloat(image.width),
            imageHeight: CGFloat(image.height),
            in: bounds
        )
        ctx.interpolationQuality = .medium
        ctx.draw(image, in: dest)
    }

    static func aspectFit(imageWidth: CGFloat, imageHeight: CGFloat, in rect: NSRect) -> CGRect {
        guard imageWidth > 0, imageHeight > 0 else { return .zero }
        let scale = min(rect.width / imageWidth, rect.height / imageHeight)
        let w = imageWidth * scale
        let h = imageHeight * scale
        return CGRect(
            x: rect.midX - w / 2,
            y: rect.midY - h / 2,
            width: w,
            height: h
        )
    }
}

struct FrameDisplayView: NSViewRepresentable {
    let cgImage: CGImage?

    func makeNSView(context: Context) -> FrameCGView {
        let view = FrameCGView()
        view.wantsLayer = true
        return view
    }

    func updateNSView(_ nsView: FrameCGView, context: Context) {
        nsView.cgImage = cgImage
    }
}
#endif
