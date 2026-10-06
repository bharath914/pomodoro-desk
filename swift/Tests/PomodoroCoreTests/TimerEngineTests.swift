import XCTest
@testable import PomodoroCore

// Mirrors core/tests/TimerEngineTests.cpp — keep both suites in sync.
final class TimerEngineTests: XCTestCase {
    func testInitialState() {
        let t = TimerEngine()
        XCTAssertEqual(t.mode, .focus)
        XCTAssertEqual(t.state, .ready)
        XCTAssertEqual(t.secsLeft(now: 0), 25 * 60)
        XCTAssertEqual(t.completedSessions, 0)
        XCTAssertEqual(t.cyclePosition, 0)
        XCTAssertFalse(t.isRunning)
    }

    func testDurationForMode() {
        XCTAssertEqual(TimerEngine.durationForMode(.focus), 25 * 60_000)
        XCTAssertEqual(TimerEngine.durationForMode(.shortBreak), 5 * 60_000)
        XCTAssertEqual(TimerEngine.durationForMode(.longBreak), 15 * 60_000)
    }

    func testStartCountsDown() {
        let t = TimerEngine()
        t.start(now: 0)
        XCTAssertTrue(t.isRunning)
        XCTAssertEqual(t.secsLeft(now: 10_000), 25 * 60 - 10)
    }

    func testPausePreservesRemaining() {
        let t = TimerEngine()
        t.start(now: 0)
        t.pause(now: 10_000)
        XCTAssertFalse(t.isRunning)
        XCTAssertEqual(t.secsLeft(now: 10_000), 25 * 60 - 10)
        XCTAssertEqual(t.secsLeft(now: 999_999), 25 * 60 - 10)  // time passing while paused changes nothing
    }

    func testResumeContinuesFromPausedRemaining() {
        let t = TimerEngine()
        t.start(now: 0)
        t.pause(now: 10_000)
        t.start(now: 50_000)  // resume at a later wall-clock time
        XCTAssertEqual(t.secsLeft(now: 50_000), 25 * 60 - 10)
        XCTAssertEqual(t.secsLeft(now: 55_000), 25 * 60 - 15)
    }

    func testToggleRoundTrip() {
        let t = TimerEngine()
        t.toggle(now: 0)        // start
        t.toggle(now: 5_000)    // pause
        let remainAfterFirstRound = t.secsLeft(now: 5_000)
        t.toggle(now: 100_000)  // resume
        t.toggle(now: 100_000)  // pause immediately, no time elapsed while running
        XCTAssertEqual(t.secsLeft(now: 100_000), remainAfterFirstRound)
    }

    func testSelectModeResetsPaused() {
        let t = TimerEngine()
        t.start(now: 0)
        t.selectMode(.shortBreak)
        XCTAssertFalse(t.isRunning)
        XCTAssertEqual(t.mode, .shortBreak)
        XCTAssertEqual(t.secsLeft(now: 0), 5 * 60)
        XCTAssertEqual(t.state, .ready)
    }

    func testResetRevertsToCurrentModeFullDuration() {
        let t = TimerEngine()
        t.start(now: 0)
        t.update(now: 10_000)
        t.reset()
        XCTAssertFalse(t.isRunning)
        XCTAssertEqual(t.secsLeft(now: 10_000), 25 * 60)
    }

    func testFocusCompletionAdvancesToShortBreak() {
        let t = TimerEngine()
        t.start(now: 0)
        XCTAssertTrue(t.update(now: 25 * 60_000))
        XCTAssertEqual(t.mode, .shortBreak)
        XCTAssertEqual(t.completedSessions, 1)
        XCTAssertEqual(t.cyclePosition, 1)
        XCTAssertFalse(t.isRunning)
        XCTAssertEqual(t.secsLeft(now: 25 * 60_000), 5 * 60)
    }

    func testShortBreakCompletionReturnsToFocus() {
        let t = TimerEngine()
        t.selectMode(.shortBreak)
        t.start(now: 0)
        t.update(now: 5 * 60_000)
        XCTAssertEqual(t.mode, .focus)
        XCTAssertEqual(t.cyclePosition, 0)  // unaffected by a break completing (no focus session completed here)
        XCTAssertEqual(t.secsLeft(now: 5 * 60_000), 25 * 60)
    }

    func testFourthFocusSessionAdvancesToLongBreak() {
        let t = TimerEngine()
        var now: Int64 = 0
        for _ in 0..<3 {
            t.selectMode(.focus)
            t.start(now: now)
            now += 25 * 60_000
            t.update(now: now)
            XCTAssertEqual(t.mode, .shortBreak)
            t.selectMode(.focus)  // skip the break manually for this test
        }
        t.start(now: now)
        now += 25 * 60_000
        XCTAssertTrue(t.update(now: now))
        XCTAssertEqual(t.mode, .longBreak)
        XCTAssertEqual(t.cyclePosition, 4)
        XCTAssertEqual(t.completedSessions, 4)
    }

    func testLongBreakCompletionResetsCycle() {
        let t = TimerEngine()
        t.selectMode(.longBreak)
        t.start(now: 0)
        t.update(now: 15 * 60_000)
        XCTAssertEqual(t.mode, .focus)
        XCTAssertEqual(t.cyclePosition, 0)
    }

    func testUpdateCompletesExactlyOnce() {
        let t = TimerEngine()
        t.start(now: 0)
        XCTAssertFalse(t.update(now: 25 * 60_000 - 1000))
        XCTAssertTrue(t.update(now: 25 * 60_000))
        XCTAssertFalse(t.isRunning)
        XCTAssertFalse(t.update(now: 25 * 60_000 + 10_000))  // no re-trigger after completion
    }

    func testSecsLeftNeverNegative() {
        let t = TimerEngine()
        t.start(now: 0)
        XCTAssertEqual(t.secsLeft(now: 999 * 60_000), 0)  // way past deadline, update() not called yet
    }
}
