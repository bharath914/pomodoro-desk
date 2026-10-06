import Foundation

// Pure countdown-timer logic — no UI, no system clock calls.
// Every method takes the current time explicitly so it is fully deterministic and testable.
// This mirrors core/TimerEngine.{h,cpp} in the Windows/Linux builds; keep the semantics in sync.

public enum TimerState: Equatable {
    case ready, running, paused, done
}

public final class TimerEngine {
    private var durationMs: Int64
    private var remainMs: Int64
    private var endTime: Int64 = 0
    public private(set) var isRunning: Bool = false

    public init(durationMinutes: Int = 25) {
        durationMs = Int64(durationMinutes) * 60_000
        remainMs = durationMs
    }

    public func setDuration(minutes: Int) {
        durationMs = Int64(minutes) * 60_000
        reset()
    }

    public func reset() {
        remainMs = durationMs
        isRunning = false
    }

    public func start(now: Int64) {
        if remainMs <= 0 { remainMs = durationMs }
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

    public func addMinutes(_ minutes: Int) {
        let add = Int64(minutes) * 60_000
        if isRunning { endTime += add } else { remainMs += add }
    }

    /// Call periodically while running; returns true exactly once, the moment the countdown hits zero.
    @discardableResult
    public func update(now: Int64) -> Bool {
        guard isRunning else { return false }
        if remainingMs(now: now) <= 0 {
            isRunning = false
            remainMs = 0
            return true
        }
        return false
    }

    public func remainingMs(now: Int64) -> Int64 {
        let ms = isRunning ? (endTime - now) : remainMs
        return max(0, ms)
    }

    public func secsLeft(now: Int64) -> Int {
        Int((remainingMs(now: now) + 999) / 1000)
    }

    public var durationMinutes: Int {
        Int(durationMs / 60_000)
    }

    public var state: TimerState {
        if isRunning { return .running }
        if remainMs <= 0 { return .done }
        if remainMs < durationMs { return .paused }
        return .ready
    }
}

/// Clamp a user-entered minute value to the valid [1, 999] range.
public func clampMinutes(_ raw: Int) -> Int {
    min(max(raw, 1), 999)
}
