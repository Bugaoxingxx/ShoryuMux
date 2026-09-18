import Foundation
import Darwin

// MARK: - Fixed paths & constants
enum Paths {
    static let label   = "com.shoryumux.daemon"
    static let cfgDir  = NSHomeDirectory() + "/.config/shoryumux"
    static let config  = cfgDir + "/config.conf"
    static let pidFile = cfgDir + "/shoryumux.pid"
    static let events  = cfgDir + "/events.log"
    static let binDir  = NSHomeDirectory() + "/Library/Application Support/ShoryuMux"
    static let daemon  = binDir + "/shoryumuxd"
    static let plist   = NSHomeDirectory() + "/Library/LaunchAgents/" + label + ".plist"
    static var uidString: String { String(getuid()) }
    static var domain: String { "gui/\(uidString)" }
    static var projectPath: String { BuildConfig.projectPath }
}

// All mappable buttons, in a stable order.
let BUTTONS = ["UP","DOWN","LEFT","RIGHT","START","BACK","L3","R3",
               "LB","RB","GUIDE","A","B","X","Y","LT","RT"]

// MARK: - Shell helper
@discardableResult
func shell(_ launchPath: String, _ args: [String]) -> (out: String, code: Int32) {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: launchPath)
    p.arguments = args
    let pipe = Pipe()
    p.standardOutput = pipe
    p.standardError = pipe
    do { try p.run() } catch {
        return ("\(launchPath) failed: \(error)", -1)
    }
    let data = pipe.fileHandleForReading.readDataToEndOfFile()
    p.waitUntilExit()
    return (String(data: data, encoding: .utf8) ?? "", p.terminationStatus)
}

// MARK: - Config load / save
struct Config {
    static func load() -> [String: String] {
        var map: [String: String] = [:]
        guard let text = try? String(contentsOfFile: Paths.config, encoding: .utf8) else { return map }
        for raw in text.components(separatedBy: "\n") {
            var line = raw
            if let h = line.firstIndex(of: "#") { line = String(line[..<h]) }
            guard let eq = line.firstIndex(of: "=") else { continue }
            let name = line[..<eq].trimmingCharacters(in: .whitespaces).uppercased()
            let value = line[line.index(after: eq)...].trimmingCharacters(in: .whitespaces)
            if BUTTONS.contains(name) { map[name] = value }
        }
        return map
    }

    static func save(_ map: [String: String]) throws {
        var out = """
        # ShoryuMux config — managed by the menu-bar app (also hand-editable).
        #   BUTTON = keys: <token> [token ...]   |   BUTTON = shell: <command>
        # Tokens: a-z 0-9 - = [ ] \\ ; ' , . / `  enter tab space esc delete fdel
        #         up down left right home end pgup pgdn f1..f12
        # Combos: cmd+c  shift+up  ctrl+alt+t   (modifiers: cmd ctrl opt/alt shift)
        # Buttons: \(BUTTONS.joined(separator: " "))
        # Keystrokes go to the FRONTMOST app — keep cmux focused when you press.

        """
        for b in BUTTONS {
            if let v = map[b]?.trimmingCharacters(in: .whitespaces), !v.isEmpty {
                out += "\(b.padding(toLength: 6, withPad: " ", startingAt: 0))= \(v)\n"
            }
        }
        try? FileManager.default.createDirectory(atPath: Paths.cfgDir, withIntermediateDirectories: true)
        try out.write(toFile: Paths.config, atomically: true, encoding: .utf8)
    }
}

// MARK: - Daemon control
struct Daemon {
    static func pid() -> pid_t? {
        guard let s = try? String(contentsOfFile: Paths.pidFile, encoding: .utf8)
                .trimmingCharacters(in: .whitespacesAndNewlines),
              let p = pid_t(s) else { return nil }
        return p
    }
    static func isRunning() -> Bool {
        guard let p = pid() else { return false }
        return kill(p, 0) == 0
    }
    static var isInstalled: Bool { FileManager.default.fileExists(atPath: Paths.plist) }
    static var hasBinary: Bool { FileManager.default.fileExists(atPath: Paths.daemon) }

    static func reload() {
        let r = shell("/bin/launchctl", ["kickstart", "-k", "\(Paths.domain)/\(Paths.label)"])
        if r.code != 0, let p = pid() { kill(p, SIGHUP) }
    }
    static func start() {
        if isInstalled {
            shell("/bin/launchctl", ["bootstrap", Paths.domain, Paths.plist])
            shell("/bin/launchctl", ["kickstart", "-k", "\(Paths.domain)/\(Paths.label)"])
        }
    }
    static func stop() {
        if let p = pid() { kill(p, SIGTERM) }
        shell("/bin/launchctl", ["kill", "SIGTERM", "\(Paths.domain)/\(Paths.label)"])
    }
    static func install() {
        shell("/bin/bash", [Paths.projectPath + "/scripts/install-launchagent.sh"])
    }
    static func uninstall() {
        shell("/bin/bash", [Paths.projectPath + "/scripts/uninstall-launchagent.sh"])
    }
    static func openAccessibilitySettings() {
        shell("/usr/bin/open", ["x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"])
    }
    static func revealDaemon() {
        shell("/usr/bin/open", ["-R", Paths.daemon])
    }
}

// MARK: - Recent events
struct StickEvent: Identifiable {
    let id = UUID()
    let button: String
    let action: String
    let when: Date
}
func recentEvents(_ n: Int = 8) -> [StickEvent] {
    guard let text = try? String(contentsOfFile: Paths.events, encoding: .utf8) else { return [] }
    let lines = text.components(separatedBy: "\n").filter { !$0.isEmpty }
    return lines.suffix(n).reversed().compactMap { line in
        let parts = line.components(separatedBy: "\t")
        guard parts.count >= 3, let ms = Double(parts[0]) else { return nil }
        return StickEvent(button: parts[1], action: parts[2],
                          when: Date(timeIntervalSince1970: ms / 1000.0))
    }
}
