import SwiftUI
import Combine

final class AppModel: ObservableObject {
    @Published var running = false
    @Published var stickReady = false
    @Published var axTrusted = true
    @Published var installed = false
    @Published var hasBinary = false
    @Published var events: [StickEvent] = []
    @Published var map: [String: String] = [:]
    @Published var statusMsg = ""
    @Published var busy = false

    private var timer: Timer?

    init() {
        map = Config.load()
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            self?.refresh()
        }
    }

    func refresh() {
        running = Daemon.isRunning()
        installed = Daemon.isInstalled
        hasBinary = Daemon.hasBinary
        events = recentEvents(8)
        if running {
            let snap = Daemon.snapshot()
            stickReady = snap.state == "ready"
            axTrusted = snap.ax
        } else {
            stickReady = false
            axTrusted = true
        }
    }

    private func later(_ s: Double, _ body: @escaping () -> Void) {
        DispatchQueue.main.asyncAfter(deadline: .now() + s) { body() }
    }

    func save() {
        do {
            try Config.save(map)
            Daemon.reload()
            statusMsg = "Saved & reloaded."
            later(0.8) { self.refresh() }
        } catch {
            statusMsg = "Save failed: \(error.localizedDescription)"
        }
    }
    func revert() { map = Config.load(); statusMsg = "Reverted from disk." }
    func reloadOnly() { Daemon.reload(); statusMsg = "Config reload requested."; later(0.8) { self.refresh() } }
    func start() { Daemon.start(); statusMsg = "Start requested."; later(0.8) { self.refresh() } }
    func stop() { Daemon.stop(); statusMsg = "Stop requested."; later(0.6) { self.refresh() } }
    func install() {
        busy = true
        statusMsg = "Installing launch agent…"
        DispatchQueue.global(qos: .userInitiated).async {
            Daemon.install()
            DispatchQueue.main.async {
                self.busy = false
                self.statusMsg = "Installed."
                self.refresh()
            }
        }
    }
    func uninstall() {
        busy = true
        statusMsg = "Uninstalling…"
        DispatchQueue.global(qos: .userInitiated).async {
            Daemon.uninstall()
            DispatchQueue.main.async {
                self.busy = false
                self.statusMsg = "Uninstalled."
                self.refresh()
            }
        }
    }
}
