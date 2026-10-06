import AppKit
import CoreText
import SwiftUI

// Minimal always-on-top overlay, mirroring the Windows/Linux builds: borderless-looking
// but natively resizable, floats above other windows, draggable by its background.

func registerBundledFont() {
    guard let url = Bundle.module.url(forResource: "DigitalNumbers-Regular", withExtension: "ttf") else { return }
    CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
}

registerBundledFont()

let app = NSApplication.shared
app.setActivationPolicy(.accessory)  // no Dock icon, no app menu — lightweight overlay

let viewModel = TimerViewModel()

let window = NSWindow(
    contentRect: NSRect(x: 100, y: 100, width: 320, height: 198),
    styleMask: [.titled, .resizable, .fullSizeContentView],
    backing: .buffered,
    defer: false
)
window.titlebarAppearsTransparent = true
window.titleVisibility = .hidden
window.standardWindowButton(.closeButton)?.isHidden = true
window.standardWindowButton(.miniaturizeButton)?.isHidden = true
window.standardWindowButton(.zoomButton)?.isHidden = true
window.isOpaque = true
window.backgroundColor = .black
window.level = .floating
window.isMovableByWindowBackground = true
window.minSize = NSSize(width: 220, height: 150)
window.contentView = NSHostingView(rootView: ContentView(vm: viewModel))
window.makeKeyAndOrderFront(nil)

app.run()
