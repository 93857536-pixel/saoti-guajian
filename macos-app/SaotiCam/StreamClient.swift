import Combine
import Foundation

protocol StreamClient: ObservableObject {
    var currentFrame: PlatformImage? { get }
    var isConnected: Bool { get }
    var statusText: String { get }
    var frameCount: Int { get }
}

extension MjpegStreamClient: StreamClient {}
extension SerialStreamClient: StreamClient {}
