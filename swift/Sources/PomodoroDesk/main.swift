import AppKit
import SwiftUI

// Always-on-top Pomodoro overlay, mirroring the Windows/Linux builds. Uses a normal titled,
// resizable NSWindow with the title bar visually blended away — this keeps the native close/
// miniaturize buttons and Dock presence for free, unlike a fully borderless window.

let app = NSApplication.shared
app.setActivationPolicy(.regular)  // Dock + Cmd-Tab visible, so it's easy to find and restore

let viewModel = TimerViewModel()

let window = NSWindow(
    contentRect: NSRect(x: 100, y: 100, width: 466, height: 568),
    styleMask: [.titled, .resizable, .closable, .miniaturizable, .fullSizeContentView],
    backing: .buffered,
    defer: false
)
window.title = "Pomodoro Desk"
window.titlebarAppearsTransparent = true
window.titleVisibility = .hidden
window.isOpaque = true
window.backgroundColor = .black
window.level = .floating
window.isMovableByWindowBackground = true
window.minSize = NSSize(width: 300, height: 360)
window.contentAspectRatio = NSSize(width: 466, height: 568)
window.contentView = NSHostingView(rootView: ContentView(vm: viewModel))
window.makeKeyAndOrderFront(nil)
app.activate(ignoringOtherApps: true)

app.run()
