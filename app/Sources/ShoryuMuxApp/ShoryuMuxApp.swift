import SwiftUI
import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationWillTerminate(_ notification: Notification) {
        Daemon.stop()
    }
}

@main
struct ShoryuMuxApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = AppModel()

    var body: some Scene {
        MenuBarExtra {
            ContentView().environmentObject(model)
        } label: {
            Image(systemName: model.running ? "gamecontroller.fill" : "gamecontroller")
        }
        .menuBarExtraStyle(.window)
    }
}
