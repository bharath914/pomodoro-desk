// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "PomodoroDesk",
    platforms: [.macOS(.v12)],
    targets: [
        .target(name: "PomodoroCore"),
        .executableTarget(name: "PomodoroDesk", dependencies: ["PomodoroCore"]),
        .testTarget(name: "PomodoroCoreTests", dependencies: ["PomodoroCore"]),
    ]
)
