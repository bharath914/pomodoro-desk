import Foundation
import AppKit
import PomodoroCore

/// Bridges the pure PomodoroCore.TimerEngine to SwiftUI: owns the wall clock and the
/// repeating tick, and republishes state as @Published properties for the view to bind to.
final class TimerViewModel: ObservableObject {
    private let engine = TimerEngine()
    private var timer: Timer?

    @Published var displayText: String = "25:00"
    @Published var startLabel: String = "Start"
    @Published var modeLabel: String = "Focus"
    @Published var mode: Mode = .focus
    @Published var isRunning: Bool = false
    @Published var progress: Double = 0
    @Published var cyclePosition: Int = 0
    @Published var completedSessions: Int = 0

    init() {
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
            self?.tick()
        }
    }

    private func now() -> Int64 {
        Int64(Date().timeIntervalSince1970 * 1000)
    }

    private func tick() {
        if engine.isRunning, engine.update(now: now()) {
            NSSound.beep()
        }
        refresh()
    }

    private func refresh() {
        let n = now()
        let s = engine.secsLeft(now: n)
        displayText = String(format: "%02d:%02d", s / 60, s % 60)
        isRunning = engine.isRunning
        mode = engine.mode
        cyclePosition = engine.cyclePosition
        completedSessions = engine.completedSessions

        let total = Double(TimerEngine.durationForMode(engine.mode))
        let remain = Double(engine.remainingMs(now: n))
        progress = total > 0 ? 1.0 - (remain / total) : 0

        switch engine.state {
        case .running: startLabel = "Pause"
        case .paused:  startLabel = "Resume"
        case .ready:   startLabel = "Start"
        }
        switch engine.mode {
        case .focus:      modeLabel = "Focus"
        case .shortBreak: modeLabel = "Short break"
        case .longBreak:  modeLabel = "Long break"
        }
    }

    func toggle() {
        engine.toggle(now: now())
        refresh()
    }

    func reset() {
        engine.reset()
        refresh()
    }

    func selectMode(_ m: Mode) {
        engine.selectMode(m)
        refresh()
    }
}
