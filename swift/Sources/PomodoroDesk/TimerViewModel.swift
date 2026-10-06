import Foundation
import AppKit
import PomodoroCore

/// Bridges the pure PomodoroCore.TimerEngine to SwiftUI: owns the wall clock and the
/// repeating tick, and republishes state as @Published properties for the view to bind to.
final class TimerViewModel: ObservableObject {
    private let engine = TimerEngine(durationMinutes: 25)
    private var timer: Timer?

    @Published var displayText: String = "25:00"
    @Published var startLabel: String = "Start"
    @Published var isRunning: Bool = false

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
        let s = engine.secsLeft(now: now())
        displayText = String(format: "%02d:%02d", s / 60, s % 60)
        isRunning = engine.isRunning
        switch engine.state {
        case .running: startLabel = "Pause"
        case .paused:  startLabel = "Resume"
        case .done, .ready: startLabel = "Start"
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

    func addMinutes(_ m: Int) {
        engine.addMinutes(m)
        refresh()
    }

    func setDuration(_ minutes: Int) {
        engine.setDuration(minutes: minutes)
        refresh()
    }

    func setCustomMinutes(_ raw: Int) {
        setDuration(clampMinutes(raw))
    }

    var currentDurationMinutes: Int {
        engine.durationMinutes
    }
}
