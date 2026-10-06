import SwiftUI
import AppKit

// Placeholder UI — functional only, pending the real design pass.
// Logic lives entirely in TimerViewModel/PomodoroCore; this view just binds to it.
struct ContentView: View {
    @ObservedObject var vm: TimerViewModel
    @State private var showCustom = false
    @State private var customText = ""

    var body: some View {
        VStack(spacing: 8) {
            Text(vm.stateLabel)
                .font(.caption.bold())
                .foregroundColor(.secondary)

            Text(vm.displayText)
                .font(.system(size: 34, weight: .light))
                .foregroundColor(.primary)
                .monospacedDigit()

            HStack(spacing: 6) {
                Button(action: vm.toggle) {
                    Image(systemName: vm.isRunning ? "pause.fill" : "play.fill")
                }
                Button(action: { vm.addMinutes(5) }) { Text("+5") }
                Button(action: { vm.addMinutes(10) }) { Text("+10") }
                Button(action: vm.reset) {
                    Image(systemName: "arrow.counterclockwise")
                }
                Button(action: { NSApp.terminate(nil) }) {
                    Image(systemName: "xmark")
                }
            }
            .buttonStyle(.bordered)
        }
        .padding(12)
        .frame(width: 220, height: 150)
        .background(Color(nsColor: .windowBackgroundColor))
        .contextMenu {
            Button("25 minutes") { vm.setDuration(25) }
            Button("30 minutes") { vm.setDuration(30) }
            Button("45 minutes") { vm.setDuration(45) }
            Button("1 hour") { vm.setDuration(60) }
            Button("2 hours") { vm.setDuration(120) }
            Divider()
            Button("Custom\u{2026}") {
                customText = "\(vm.currentDurationMinutes)"
                showCustom = true
            }
            Divider()
            Button("Quit") { NSApp.terminate(nil) }
        }
        .sheet(isPresented: $showCustom) {
            VStack(spacing: 10) {
                Text("Minutes (1-999)")
                TextField("25", text: $customText)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 100)
                HStack {
                    Button("Cancel") { showCustom = false }
                    Button("Set") {
                        if let v = Int(customText) { vm.setCustomMinutes(v) }
                        showCustom = false
                    }
                }
            }
            .padding(20)
        }
    }
}
