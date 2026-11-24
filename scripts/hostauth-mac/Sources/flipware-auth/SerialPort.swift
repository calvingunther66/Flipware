// Minimal POSIX serial link to the Flipper CLI (USB CDC, so baud rate is ignored).
import Darwin
import Foundation

enum SerialError: Error, CustomStringConvertible {
    case noFlipper, severalFlippers([String]), open(String, Int32), timeout(String), io(Int32), appOpen

    var description: String {
        switch self {
        case .noFlipper: return "No Flipper found (looked for /dev/cu.usbmodemflip_*)."
        case .severalFlippers(let ports): return "Several Flippers found, pick one with --port: \(ports.joined(separator: ", "))"
        case .open(let path, let err): return "Could not open \(path): \(String(cString: strerror(err)))"
        case .timeout(let what): return "Timed out waiting for \(what)."
        case .io(let err): return "Serial I/O error: \(String(cString: strerror(err)))"
        case .appOpen: return "An app is open on the Flipper. Close it (Back to the desktop), then try again."
        }
    }
}

final class SerialPort {
    private let fd: Int32
    private var buffer = ""
    let path: String

    static func flipperPorts() -> [String] {
        let dev = (try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? []
        return dev.filter { $0.hasPrefix("cu.usbmodemflip_") }.sorted().map { "/dev/" + $0 }
    }

    static func autodetect() throws -> String {
        let ports = flipperPorts()
        if ports.isEmpty { throw SerialError.noFlipper }
        if ports.count > 1 { throw SerialError.severalFlippers(ports) }
        return ports[0]
    }

    init(path: String) throws {
        self.path = path
        fd = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        if fd < 0 { throw SerialError.open(path, errno) }

        var tio = termios()
        tcgetattr(fd, &tio)
        cfmakeraw(&tio)
        tio.c_cflag |= tcflag_t(CLOCAL | CREAD)
        cfsetspeed(&tio, speed_t(B115200))
        tcsetattr(fd, TCSANOW, &tio)

        // The Flipper starts its shell when DTR goes up (opening the port raises it)
        var bits: Int32 = TIOCM_DTR
        _ = ioctl(fd, TIOCMBIS, &bits)
    }

    deinit { Darwin.close(fd) }

    func write(_ line: String) throws {
        let bytes = Array((line + "\r").utf8)
        var offset = 0
        while offset < bytes.count {
            let n = bytes[offset...].withUnsafeBufferPointer { Darwin.write(fd, $0.baseAddress, $0.count) }
            if n < 0 {
                if errno == EAGAIN { usleep(1000); continue }
                throw SerialError.io(errno)
            }
            offset += n
        }
    }

    /// Read until the CLI prompt (">: ") and return the lines before it, ANSI escapes stripped.
    func readUntilPrompt(timeout: TimeInterval) throws -> [String] {
        let deadline = Date().addingTimeInterval(timeout)
        var chunk = [UInt8](repeating: 0, count: 512)
        while Date() < deadline {
            if let range = buffer.range(of: ">: ") {
                let text = String(buffer[..<range.lowerBound])
                buffer = String(buffer[range.upperBound...])
                return text.components(separatedBy: CharacterSet.newlines)
                    .map(SerialPort.stripAnsi)
                    .map { $0.trimmingCharacters(in: .whitespaces) }
                    .filter { !$0.isEmpty }
            }
            let n = Darwin.read(fd, &chunk, chunk.count)
            if n > 0 {
                buffer += String(decoding: chunk[0..<n], as: UTF8.self)
            } else if n < 0 && errno != EAGAIN {
                throw SerialError.io(errno)
            } else {
                usleep(10_000)
            }
        }
        throw SerialError.timeout("the Flipper prompt")
    }

    /// Send one command and collect its `hostauth-*` reply lines.
    func command(_ line: String, timeout: TimeInterval = 5) throws -> [ReplyLine] {
        try write(line)
        let lines = try readUntilPrompt(timeout: timeout)
        // The shell refuses `hostauth` while an app is open (it takes the loader
        // lock, so it never competes with an app for memory)
        if lines.contains(where: { $0.contains("cannot be run while an application is open") }) {
            throw SerialError.appOpen
        }
        return lines.compactMap(ReplyLine.init)
    }

    /// Wake the shell and drop the banner.
    func sync() throws {
        try write("")
        _ = try readUntilPrompt(timeout: 5)
    }

    private static func stripAnsi(_ s: String) -> String {
        s.replacingOccurrences(of: "\u{1B}\\[[0-9;?]*[A-Za-z]", with: "", options: .regularExpression)
    }
}
