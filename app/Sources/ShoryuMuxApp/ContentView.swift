import SwiftUI

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
            if !model.statusMsg.isEmpty {
                Text(model.statusMsg).font(.caption).foregroundColor(.accentColor)
            }
        }
        .padding(14)
        .frame(width: 440)
    }

    // MARK: header
    private var header: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Circle().fill(model.running ? Color.green : Color.secondary)
                    .frame(width: 9, height: 9)
                Text(model.running ? "Daemon running" : "Daemon stopped").bold()
                Spacer()
                Text(model.installed ? "at login: on" : "at login: off")
                    .font(.caption).foregroundColor(.secondary)
            }
            HStack(spacing: 8) {
                Button("Start") { model.start() }
                    .disabled(model.running || !model.installed)
                Button("Stop") { model.stop() }
                    .disabled(!model.running)
                Button("Reload") { model.reloadOnly() }
                Spacer()
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
                Spacer()
            }
            HStack {
                Button("Open Accessibility settings") { Daemon.openAccessibilitySettings() }
                Button("Reveal daemon") { Daemon.revealDaemon() }
                Spacer()
            }
            Text("Grant Accessibility to the shoryumuxd binary: click “Reveal daemon”, drag that file into the Accessibility list, then Reload. Keystrokes go to the frontmost app, so keep cmux focused.")
                .font(.caption).foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    // MARK: helpers
    private func binding(for b: String) -> Binding<String> {
        Binding(get: { model.map[b] ?? "" }, set: { model.map[b] = $0 })
    }
    private func timeString(_ d: Date) -> String {
        let f = DateFormatter(); f.dateFormat = "HH:mm:ss"; return f.string(from: d)
    }
}
