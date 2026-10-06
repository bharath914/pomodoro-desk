import Foundation

// Pure Pomodoro-cycle logic — no UI, no system clock calls.
// Every method takes the current time explicitly so it is fully deterministic and testable.
// Mirrors core/TimerEngine.{h,cpp} in the Windows/Linux builds; keep the semantics in sync.

public enum Mode: Hashable {
    case focus, shortBreak, longBreak
}

public enum RunState: Equatable {
    case ready, running, paused
}

public final class TimerEngine {
    private var mode_: Mode = .focus
    private var remainMs: Int64
    private var endTime: Int64 = 0
    public private(set) var isRunning: Bool = false
    public private(set) var completedSessions: Int = 0
    public private(set) var cyclePosition: Int = 0  // 0..4 focus sessions completed since the last long break

    public init() {
        remainMs = TimerEngine.durationForMode(.focus)
    }

    public static func durationForMode(_ m: Mode) -> Int64 {
        switch m {
        case .focus:      return 25 * 60_000
        case .shortBreak: return 5 * 60_000
        case .longBreak:  return 15 * 60_000
        }
    }

    public var mode: Mode { mode_ }

    public func selectMode(_ m: Mode) {
        mode_ = m
        remainMs = TimerEngine.durationForMode(m)
        isRunning = false
    }

    public func reset() {
        remainMs = TimerEngine.durationForMode(mode_)
        isRunning = false
    }

    public func start(now: Int64) {
        if remainMs <= 0 { remainMs = TimerEngine.durationForMode(mode_) }
        endTime = now + remainMs
        isRunning = true
    }

    public func pause(now: Int64) {
        guard isRunning else { return }
        remainMs = max(0, endTime - now)
        isRunning = false
    }

    public func toggle(now: Int64) {
        isRunning ? pause(now: now) : start(now: now)
    }

    /// Call periodically while running; returns true exactly once, the moment the countdown hits
    /// zero. By the time it returns, `mode` has already advanced to the next step of the cycle:
    /// focus -> shortBreak (or longBreak every 4th) -> focus, each session counted and paused,
    /// ready for the next Start press.
    @discardableResult
    public func update(now: Int64) -> Bool {
        guard isRunning, remainingMs(now: now) <= 0 else { return false }

        isRunning = false
        if mode_ == .focus {
            completedSessions += 1
            cyclePosition += 1
            mode_ = cyclePosition >= 4 ? .longBreak : .shortBreak
        } else {
            if mode_ == .longBreak { cyclePosition = 0 }
            mode_ = .focus
        }
        remainMs = TimerEngine.durationForMode(mode_)
        return true
    }

    public func remainingMs(now: Int64) -> Int64 {
        let ms = isRunning ? (endTime - now) : remainMs
        return max(0, ms)
    }

    public func secsLeft(now: Int64) -> Int {
        Int((remainingMs(now: now) + 999) / 1000)
    }

    public var state: RunState {
        if isRunning { return .running }
        if remainMs < TimerEngine.durationForMode(mode_) { return .paused }
        return .ready
    }
}
