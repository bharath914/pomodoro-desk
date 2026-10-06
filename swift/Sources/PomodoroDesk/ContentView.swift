import SwiftUI
import PomodoroCore

// Base design size (logical points); the whole layout scales uniformly to fit
// whatever size the window is resized to, mirroring the Windows/Linux layout math.
// (On macOS the window itself is aspect-locked via contentAspectRatio, so in practice
// width and height always grow together — this scale factor just tracks that.)
private let baseW: CGFloat = 466
private let baseH: CGFloat = 568

private let segTrackColor = Color(red: 29.0 / 255, green: 29.0 / 255, blue: 31.0 / 255)
private let resetBgColor = Color(red: 21.0 / 255, green: 21.0 / 255, blue: 23.0 / 255)
private let ringColor = Color(red: 58.0 / 255, green: 57.0 / 255, blue: 62.0 / 255)
private let secondaryText = Color(red: 155.0 / 255, green: 155.0 / 255, blue: 158.0 / 255)

struct ContentView: View {
    @ObservedObject var vm: TimerViewModel

    var body: some View {
        GeometryReader { geo in
            let scale = min(geo.size.width / baseW, geo.size.height / baseH)

            ZStack {
                Color.black

                tabBar(scale: scale)
                    .position(x: geo.size.width / 2, y: (54 + 28) * scale)

                ZStack {
                    Circle()
                        .stroke(ringColor, lineWidth: 13 * scale)
                    Circle()
                        .trim(from: 0, to: vm.progress)
                        .stroke(Color.white, style: StrokeStyle(lineWidth: 13 * scale, lineCap: .butt))
                        .rotationEffect(.degrees(-90))
                    VStack(spacing: 4 * scale) {
                        Text(vm.displayText)
                            .font(.system(size: 46 * scale, weight: .bold))
                            .foregroundColor(.white)
                            .monospacedDigit()
                        Text(vm.modeLabel)
                            .font(.system(size: 14 * scale))
                            .foregroundColor(secondaryText)
                    }
                }
                .frame(width: 230 * scale, height: 230 * scale)
                .position(x: geo.size.width / 2, y: 264 * scale)

                HStack(spacing: 12 * scale) {
                    pill(vm.startLabel, bg: .white, fg: .black, scale: scale, action: vm.toggle)
                    pill("Reset", bg: resetBgColor, fg: secondaryText, scale: scale, action: vm.reset)
                }
                .position(x: geo.size.width / 2, y: (420 + 29.5) * scale)

                sessionsRow(scale: scale)
                    .position(x: geo.size.width / 2, y: 523 * scale)
            }
        }
        .frame(minWidth: 300, minHeight: 360)
    }

    private func tabBar(scale: CGFloat) -> some View {
        HStack(spacing: 0) {
            ForEach([Mode.focus, .shortBreak, .longBreak], id: \.self) { m in
                let selected = vm.mode == m
                Text(label(for: m))
                    .font(.system(size: 13 * scale, weight: .semibold))
                    .foregroundColor(selected ? .black : secondaryText)
                    .padding(.horizontal, 14 * scale)
                    .frame(height: 46 * scale)
                    .background(selected ? Capsule().fill(Color.white) : nil)
                    .onTapGesture { vm.selectMode(m) }
            }
        }
        .padding(5 * scale)
        .background(Capsule().fill(segTrackColor))
    }

    private func label(for m: Mode) -> String {
        switch m {
        case .focus: return "Focus"
        case .shortBreak: return "Short break"
        case .longBreak: return "Long break"
        }
    }

    private func pill(_ text: String, bg: Color, fg: Color, scale: CGFloat, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.system(size: 15 * scale, weight: .semibold))
                .foregroundColor(fg)
                .frame(width: 120 * scale, height: 59 * scale)
                .background(Capsule().fill(bg))
        }
        .buttonStyle(.plain)
    }

    private func sessionsRow(scale: CGFloat) -> some View {
        HStack(spacing: 8 * scale) {
            HStack(spacing: 8 * scale) {
                ForEach(0..<4) { i in
                    Circle()
                        .fill(i < vm.cyclePosition ? Color.white : ringColor)
                        .frame(width: 12 * scale, height: 12 * scale)
                }
            }
            Text("\(vm.completedSessions) session\(vm.completedSessions == 1 ? "" : "s")")
                .font(.system(size: 12 * scale))
                .foregroundColor(secondaryText)
        }
    }
}
