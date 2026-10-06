import AppKit
import SwiftUI

// Minimal always-on-top overlay, mirroring the Windows/Linux builds: borderless,
// draggable, floats above other windows. Visual design is a placeholder pending
// the upcoming redesign — only the logic/behavior parity matters right now.

let app = NSApplication.shared
app.setActivationPolicy(.accessory)  // no Dock icon, no app menu

let viewModel = TimerViewModel()

let window = NSWindow(
    contentRect: NSRect(x: 100, y: 100, width: 220, height: 150),
    styleMask: [.borderless],
    backing: .buffered,
    defer: false
)
window.isOpaque = false
window.backgroundColor = .clear
window.level = .floating
window.isMovableByWindowBackground = true
window.hasShadow = true
window.contentView = NSHostingView(rootView: ContentView(vm: viewModel))
window.makeKeyAndOrderFront(nil)

app.run()
