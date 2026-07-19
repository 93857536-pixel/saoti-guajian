import CoreBluetooth
import Foundation

enum SaotiBle {
    static let service = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    static let status = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    static let command = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
    static let answer = CBUUID(string: "6E400004-B5A3-F393-E0A9-E50E24DCCA9E")
    static let thumb = CBUUID(string: "6E400005-B5A3-F393-E0A9-E50E24DCCA9E")
    static let event = CBUUID(string: "6E400006-B5A3-F393-E0A9-E50E24DCCA9E")
    static let namePrefix = "Saoti-"
    /// 广播厂商数据魔数（跳过 2 字节 company id 后）
    static let mfgMagic = Data([0x53, 0x41, 0x4F, 0x54]) // "SAOT"
    static let protoVersion = 1

    static func matchesManufacturer(_ data: Data?) -> Bool {
        guard let data, data.count >= 6 else { return false }
        // bytes 0-1 company, 2-5 "SAOT"
        return data.subdata(in: 2..<6) == mfgMagic
    }
}
