import Foundation

#if canImport(AppKit)
import AppKit
import ImageIO
import CoreImage
typealias PlatformImage = NSImage
#else
import UIKit
typealias PlatformImage = UIImage
#endif

extension PlatformImage {
    #if canImport(AppKit)
    static func cgImageFromRGB565Payload(_ payload: Data) -> CGImage? {
        guard payload.count >= 4 else { return nil }
        let width = Int(payload[0]) | (Int(payload[1]) << 8)
        let height = Int(payload[2]) | (Int(payload[3]) << 8)
        guard width > 1, height > 1 else { return nil }
        let pixelBytes = width * height * 2
        guard payload.count >= 4 + pixelBytes else { return nil }

        var rgba = [UInt8](repeating: 255, count: width * height * 4)
        payload.withUnsafeBytes { raw in
            guard let base = raw.baseAddress?.advanced(by: 4) else { return }
            let pixels = base.assumingMemoryBound(to: UInt16.self)
            for i in 0..<(width * height) {
                let value = Int(UInt16(littleEndian: pixels[i]))
                let r5 = (value >> 11) & 0x1F
                let g6 = (value >> 5) & 0x3F
                let b5 = value & 0x1F
                let o = i * 4
                rgba[o] = UInt8((r5 * 255) / 31)
                rgba[o + 1] = UInt8((g6 * 255) / 63)
                rgba[o + 2] = UInt8((b5 * 255) / 31)
            }
        }
        guard let provider = CGDataProvider(data: Data(rgba) as CFData) else { return nil }
        return CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        )
    }

    static func cgImageFromJPEGData(_ payload: Data) -> CGImage? {
        let candidates = [repairedJPEGData(payload), normalizedJPEGData(payload), payload].compactMap { $0 }
        for data in candidates where data.count >= 128 {
            if let cg = decodeJPEGPayloadViaPillowToCGImage(data) { return cg }
            if let cg = decodeJPEGPayloadViaFFmpegToCGImage(data) { return cg }
            if let cg = decodeJPEGPayloadViaImageIO(data) { return cg }
        }
        return nil
    }

    private static func decodeJPEGPayloadViaImageIO(_ payload: Data) -> CGImage? {
        guard let src = CGImageSourceCreateWithData(payload as CFData, nil),
              let cg = CGImageSourceCreateImageAtIndex(src, 0, nil),
              cg.width > 1, cg.height > 1 else { return nil }
        return cg
    }

    private static func cgImageFromPNGFile(_ path: String) -> CGImage? {
        guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
              let cg = CGImageSourceCreateImageAtIndex(src, 0, nil),
              cg.width > 1, cg.height > 1 else { return nil }
        return cg
    }

    private static func decodeJPEGPayloadViaPillowToCGImage(_ payload: Data) -> CGImage? {
        let id = UUID().uuidString
        let jpegPath = NSTemporaryDirectory() + "saoticam-\(id).jpg"
        let pngPath = NSTemporaryDirectory() + "saoticam-\(id).png"
        defer {
            try? FileManager.default.removeItem(atPath: jpegPath)
            try? FileManager.default.removeItem(atPath: pngPath)
        }
        guard (payload as NSData).write(toFile: jpegPath, atomically: true) else { return nil }
        let script = """
import sys
from PIL import Image, ImageFile
ImageFile.LOAD_TRUNCATED_IMAGES = True
im = Image.open(sys.argv[1])
im.load()
im.save(sys.argv[2])
"""
        let home = NSHomeDirectory()
        let userSite = "\(home)/Library/Python/3.9/lib/python/site-packages"
        let interpreters = ["/usr/bin/python3", "/opt/homebrew/bin/python3", "/usr/local/bin/python3"]
        for python in interpreters where FileManager.default.isExecutableFile(atPath: python) {
            var env = ProcessInfo.processInfo.environment
            if FileManager.default.fileExists(atPath: userSite) {
                env["PYTHONPATH"] = env["PYTHONPATH"].map { userSite + ":" + $0 } ?? userSite
            }
            let proc = Process()
            proc.executableURL = URL(fileURLWithPath: python)
            proc.arguments = ["-c", script, jpegPath, pngPath]
            proc.environment = env
            do {
                try proc.run()
                proc.waitUntilExit()
            } catch {
                continue
            }
            if proc.terminationStatus == 0, let cg = cgImageFromPNGFile(pngPath) {
                return cg
            }
        }
        return nil
    }

    private static func decodeJPEGPayloadViaFFmpegToCGImage(_ payload: Data) -> CGImage? {
        let id = UUID().uuidString
        let jpegPath = NSTemporaryDirectory() + "saoticam-\(id).jpg"
        let pngPath = NSTemporaryDirectory() + "saoticam-\(id).png"
        defer {
            try? FileManager.default.removeItem(atPath: jpegPath)
            try? FileManager.default.removeItem(atPath: pngPath)
        }
        guard (payload as NSData).write(toFile: jpegPath, atomically: true) else { return nil }
        let candidates = ["/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"]
        guard let ffmpeg = candidates.first(where: { FileManager.default.isExecutableFile(atPath: $0) }) else {
            return nil
        }
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: ffmpeg)
        proc.arguments = ["-hide_banner", "-loglevel", "error", "-y", "-i", jpegPath, pngPath]
        do {
            try proc.run()
            proc.waitUntilExit()
        } catch {
            return nil
        }
        guard proc.terminationStatus == 0 else { return nil }
        return cgImageFromPNGFile(pngPath)
    }
    #endif

    static func fromRGB565Payload(_ payload: Data) -> PlatformImage? {
        guard payload.count >= 4 else { return nil }
        let width = Int(payload[0]) | (Int(payload[1]) << 8)
        let height = Int(payload[2]) | (Int(payload[3]) << 8)
        guard width > 1, height > 1 else { return nil }
        let pixelBytes = width * height * 2
        guard payload.count >= 4 + pixelBytes else { return nil }

        var rgba = [UInt8](repeating: 255, count: width * height * 4)
        payload.withUnsafeBytes { raw in
            guard let base = raw.baseAddress?.advanced(by: 4) else { return }
            let pixels = base.assumingMemoryBound(to: UInt16.self)
            for i in 0..<(width * height) {
                let value = Int(UInt16(littleEndian: pixels[i]))
                let r5 = (value >> 11) & 0x1F
                let g6 = (value >> 5) & 0x3F
                let b5 = value & 0x1F
                let o = i * 4
                rgba[o] = UInt8((r5 * 255) / 31)
                rgba[o + 1] = UInt8((g6 * 255) / 63)
                rgba[o + 2] = UInt8((b5 * 255) / 31)
            }
        }

        #if canImport(AppKit)
        guard let provider = CGDataProvider(data: Data(rgba) as CFData) else { return nil }
        guard let cgImage = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        ) else {
            return nil
        }
        return NSImage(cgImage: cgImage, size: NSSize(width: width, height: height))
        #else
        return nil
        #endif
    }

    static func normalizedJPEGData(_ data: Data) -> Data? {
        let soi = Data([0xFF, 0xD8])
        let eoi = Data([0xFF, 0xD9])
        guard let soiRange = data.range(of: soi) else { return nil }
        let tail = data[soiRange.lowerBound...]
        guard let eoiRange = tail.range(of: eoi, options: .backwards) else { return nil }
        let trimmed = Data(tail[..<eoiRange.upperBound])
        guard trimmed.count >= 128 else { return nil }
        return trimmed
    }

    static func repairedJPEGData(_ data: Data) -> Data? {
        guard let trimmed = normalizedJPEGData(data) else { return nil }
        var out = Data()
        out.reserveCapacity(trimmed.count)
        var i = trimmed.startIndex
        while i < trimmed.endIndex {
            let b = trimmed[i]
            if b == 0, trimmed.index(after: i) < trimmed.endIndex, trimmed[trimmed.index(after: i)] == 0xFF {
                i = trimmed.index(after: i)
                continue
            }
            if b == 0xFF, trimmed.index(after: i) < trimmed.endIndex {
                let j = trimmed.index(after: i)
                if trimmed[j] == 0xFF, trimmed.index(after: j) < trimmed.endIndex {
                    let m = trimmed[trimmed.index(after: j)]
                    if m != 0x00 && m != 0xFF && m != 0xD8 {
                        out.append(0xFF)
                        out.append(m)
                        i = trimmed.index(after: trimmed.index(after: j))
                        continue
                    }
                }
            }
            out.append(b)
            i = trimmed.index(after: i)
        }
        return out.count >= 128 ? out : nil
    }

    static func fromJPEGData(_ data: Data) -> PlatformImage? {
        #if canImport(AppKit)
        guard let cg = cgImageFromJPEGData(data) else { return nil }
        return NSImage(cgImage: cg, size: NSSize(width: cg.width, height: cg.height))
        #else
        guard let image = UIImage(data: data), image.size.width > 1, image.size.height > 1 else {
            return nil
        }
        return image
        #endif
    }
}
