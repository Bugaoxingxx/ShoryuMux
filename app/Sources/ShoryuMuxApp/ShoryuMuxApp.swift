import SwiftUI

@main
struct ShoryuMuxApp: App {
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
