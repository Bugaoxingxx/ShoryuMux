import SwiftUI
import AppKit

struct ContentView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            header
            Divider()
            mappingSection
            Divider()
            eventsSection
            Divider()
            setupSection
            Divider()
            HStack {
                if !model.statusMsg.isEmpty {
                    Text(model.statusMsg).font(.caption).foregroundColor(.accentColor)
                    Spacer()
                } else {
                    Spacer()
                }
                Button("Quit ShoryuMux") { NSApplication.shared.terminate(nil) }
                    .keyboardShortcut("q")
                    .help("Quits the app and stops the daemon")
            }
        }
        .padding(14)
        .frame(width: 440)
    }

    // MARK: header
    private var header: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Circle().fill(statusColor)
                    .frame(width: 9, height: 9)
                Text(statusTitle).bold()
                Spacer()
                Text(model.installed ? "at login: on" : "at login: off")
                    .font(.caption).foregroundColor(.secondary)
            }
            HStack(spacing: 8) {
                Button("Start") { model.start() }
                    .disabled(model.running || !model.installed || model.busy)
                Button("Stop") { model.stop() }
                    .disabled(!model.running || model.busy)
                Button("Reload") { model.reloadOnly() }
                    .disabled(model.busy)
                Spacer()
            }
            if model.running && !model.axTrusted {
                Text("Accessibility is not granted — keystrokes will not be delivered. Add shoryumuxd (Reveal daemon), then Stop and Start.")
                    .font(.caption).foregroundColor(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if !model.hasBinary {
                Text("Daemon binary not installed yet — click “Install at login” below (or run scripts/install-launchagent.sh).")
                    .font(.caption).foregroundColor(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    // MARK: mapping
    private var mappingSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Button mapping").font(.headline)
            ScrollView {
                VStack(spacing: 5) {
                    ForEach(BUTTONS, id: \.self) { b in
                        HStack(spacing: 8) {
                            Text(b)
                                .font(.system(.body, design: .monospaced))
                                .frame(width: 58, alignment: .leading)
                            TextField("keys: enter   |   shell: …", text: binding(for: b))
                                .textFieldStyle(.roundedBorder)
                                .font(.system(.body, design: .monospaced))
                        }
                    }
                }
            }
            .frame(height: 250)
            HStack {
                Button("Save & Reload") { model.save() }.keyboardShortcut("s")
                Button("Revert") { model.revert() }
                Spacer()
                Text("e.g.  keys: y enter   ·   keys: cmd+c   ·   shell: say hi")
                    .font(.caption).foregroundColor(.secondary)
            }
        }
    }

    // MARK: events
    private var eventsSection: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text("Recent presses").font(.headline)
            if model.events.isEmpty {
                Text("No events yet — press a stick button while the daemon runs.")
                    .font(.caption).foregroundColor(.secondary)
            } else {
                ForEach(model.events) { e in
                    HStack(spacing: 8) {
                        Text(e.button).font(.system(.body, design: .monospaced)).frame(width: 58, alignment: .leading)
                        Text(e.action).foregroundColor(.secondary).lineLimit(1)
                        Spacer()
                        Text(timeString(e.when)).font(.caption).foregroundColor(.secondary)
                    }
                }
            }
        }
    }

    // MARK: setup
    private var setupSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Setup").font(.headline)
            HStack {
                Button(model.installed ? "Uninstall at login" : "Install at login") {
                    model.installed ? model.uninstall() : model.install()
                }
                .disabled(model.busy)
                Spacer()
            }
            HStack {
                Button("Open Accessibility settings") { Daemon.openAccessibilitySettings() }
                Button("Reveal daemon") { Daemon.revealDaemon() }
                Spacer()
            }
            Text("Grant Accessibility to the shoryumuxd binary: click “Reveal daemon”, drag that file into the Accessibility list, then Stop and Start (Reload only re-reads the config). Keystrokes go to the frontmost app, so keep cmux focused.")
                .font(.caption).foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    // MARK: helpers
    private var statusColor: Color {
        if !model.running { return .secondary }
        if model.stickReady { return .green }
        return .orange
    }
    private var statusTitle: String {
        if !model.running { return "Daemon stopped" }
        if model.stickReady { return "Stick connected" }
        return "Waiting for stick"
    }
    private func binding(for b: String) -> Binding<String> {
        Binding(get: { model.map[b] ?? "" }, set: { model.map[b] = $0 })
    }
    private func timeString(_ d: Date) -> String {
        let f = DateFormatter(); f.dateFormat = "HH:mm:ss"; return f.string(from: d)
    }
}
