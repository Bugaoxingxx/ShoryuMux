// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "ShoryuMuxApp",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "ShoryuMuxApp",
            path: "Sources/ShoryuMuxApp"
        )
    ]
)
