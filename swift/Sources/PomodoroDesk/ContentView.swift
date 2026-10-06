import SwiftUI
import AppKit

// Base design size (logical points); the whole layout scales uniformly to fit
// whatever size the window is resized to, mirroring the Windows/Linux layout math.
private let baseW: CGFloat = 320
private let baseH: CGFloat = 198

struct ContentView: View {
    @ObservedObject var vm: TimerViewModel
    @State private var showCustom = false
    @State private var customText = ""

    var body: some View {
        GeometryReader { geo in
            let scale = min(geo.size.width / baseW, geo.size.height / baseH)

            ZStack {
                Color.black

                timeText(scale: scale)
                    .position(x: geo.size.width / 2, y: (14 + 43) * scale)

                startButton(scale: scale)
                    .position(x: geo.size.width / 2, y: (112 + 20) * scale)

                HStack(spacing: 8 * scale) {
                    stepper(scale: scale)
                    iconButton(systemName: vm.isRunning ? "pause.fill" : "play.fill", scale: scale, action: vm.toggle)
                    iconButton(systemName: "arrow.counterclockwise", scale: scale, action: vm.reset)
                }
                .position(x: geo.size.width / 2, y: (164 + 17) * scale)
            }
        }
        .frame(minWidth: 220, minHeight: 150)
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

    private func timeText(scale: CGFloat) -> some View {
        Text(vm.displayText)
            .font(.custom("Digital Numbers", size: 68 * scale))
            .foregroundColor(.white)
    }

    private func startButton(scale: CGFloat) -> some View {
        Button(action: vm.toggle) {
            Text(vm.startLabel)
                .font(.system(size: 17 * scale, weight: .semibold))
                .foregroundColor(.black)
                .frame(width: 170 * scale, height: 40 * scale)
                .background(Capsule().fill(Color.white))
        }
        .buttonStyle(.plain)
    }

    private func stepper(scale: CGFloat) -> some View {
        HStack(spacing: 0) {
            Button(action: { vm.addMinutes(-10) }) {
                Text("\u{2212}").frame(width: 37 * scale, height: 34 * scale)
            }
            Text("10m")
                .font(.system(size: 15 * scale, weight: .semibold))
            Button(action: { vm.addMinutes(10) }) {
                Text("+").frame(width: 37 * scale, height: 34 * scale)
            }
        }
        .font(.system(size: 15 * scale, weight: .semibold))
        .foregroundColor(.white)
        .frame(width: 150 * scale, height: 34 * scale)
        .background(Capsule().strokeBorder(Color.white, lineWidth: 1))
        .buttonStyle(.plain)
    }

    private func iconButton(systemName: String, scale: CGFloat, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.system(size: 14 * scale, weight: .medium))
                .foregroundColor(.white)
                .frame(width: 50 * scale, height: 34 * scale)
                .background(RoundedRectangle(cornerRadius: 10 * scale).strokeBorder(Color.white, lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}
