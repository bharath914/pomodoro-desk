import XCTest
@testable import PomodoroCore

// Mirrors core/tests/TimerEngineTests.cpp — keep both suites in sync.
final class TimerEngineTests: XCTestCase {
    func testInitialState() {
        let t = TimerEngine(durationMinutes: 25)
        XCTAssertEqual(t.state, .ready)
        XCTAssertEqual(t.secsLeft(now: 0), 25 * 60)
        XCTAssertFalse(t.isRunning)
    }

    func testStartCountsDown() {
        let t = TimerEngine(durationMinutes: 25)
        t.start(now: 0)
        XCTAssertTrue(t.isRunning)
        XCTAssertEqual(t.secsLeft(now: 10_000), 25 * 60 - 10)
    }

    func testPausePreservesRemaining() {
        let t = TimerEngine(durationMinutes: 25)
        t.start(now: 0)
        t.pause(now: 10_000)
        XCTAssertFalse(t.isRunning)
        XCTAssertEqual(t.secsLeft(now: 10_000), 25 * 60 - 10)
        XCTAssertEqual(t.secsLeft(now: 999_999), 25 * 60 - 10)  // time passing while paused changes nothing
    }

    func testResumeContinuesFromPausedRemaining() {
        let t = TimerEngine(durationMinutes: 25)
        t.start(now: 0)
        t.pause(now: 10_000)
        t.start(now: 50_000)  // resume at a later wall-clock time
        XCTAssertEqual(t.secsLeft(now: 50_000), 25 * 60 - 10)
        XCTAssertEqual(t.secsLeft(now: 55_000), 25 * 60 - 15)
    }

    func testToggleRoundTrip() {
        let t = TimerEngine(durationMinutes: 25)
        t.toggle(now: 0)        // start
        t.toggle(now: 5_000)    // pause
        let remainAfterFirstRound = t.secsLeft(now: 5_000)
        t.toggle(now: 100_000)  // resume
        t.toggle(now: 100_000)  // pause immediately, no time elapsed while running
        XCTAssertEqual(t.secsLeft(now: 100_000), remainAfterFirstRound)
    }

    func testAddMinutesWhileRunning() {
        let t = TimerEngine(durationMinutes: 25)
        t.start(now: 0)
        let before = t.secsLeft(now: 0)
        t.addMinutes(5)
        XCTAssertEqual(t.secsLeft(now: 0), before + 300)
        XCTAssertTrue(t.isRunning)
    }

    func testAddMinutesWhilePaused() {
        let t = TimerEngine(durationMinutes: 25)
        t.addMinutes(10)
        XCTAssertEqual(t.secsLeft(now: 0), 35 * 60)
        XCTAssertFalse(t.isRunning)
    }

    func testResetRevertsToFullDuration() {
        let t = TimerEngine(durationMinutes: 25)
        t.start(now: 0)
        t.update(now: 10_000)
        t.reset()
        XCTAssertFalse(t.isRunning)
        XCTAssertEqual(t.secsLeft(now: 10_000), 25 * 60)
    }

    func testSetDurationResets() {
        let t = TimerEngine(durationMinutes: 25)
        t.start(now: 0)
        t.setDuration(minutes: 45)
        XCTAssertFalse(t.isRunning)
        XCTAssertEqual(t.secsLeft(now: 0), 45 * 60)
    }

    func testUpdateCompletesExactlyOnce() {
        let t = TimerEngine(durationMinutes: 1)
        t.start(now: 0)
        XCTAssertFalse(t.update(now: 30_000))
        XCTAssertTrue(t.update(now: 60_000))
        XCTAssertEqual(t.state, .done)
        XCTAssertFalse(t.isRunning)
        XCTAssertFalse(t.update(now: 70_000))  // no re-trigger after completion
    }

    func testSecsLeftNeverNegative() {
        let t = TimerEngine(durationMinutes: 1)
        t.start(now: 0)
        XCTAssertEqual(t.secsLeft(now: 120_000), 0)  // past deadline, update() not called yet
    }

    func testDoneCanBeRevivedByAddingTime() {
        let t = TimerEngine(durationMinutes: 1)
        t.start(now: 0)
        t.update(now: 60_000)
        XCTAssertEqual(t.state, .done)
        t.addMinutes(5)
        XCTAssertGreaterThan(t.secsLeft(now: 60_000), 0)
        XCTAssertNotEqual(t.state, .done)
    }

    func testPresetDurations() {
        for minutes in [25, 30, 45, 60, 120] {
            let t = TimerEngine(durationMinutes: minutes)
            XCTAssertEqual(t.secsLeft(now: 0), minutes * 60)
        }
    }

    func testClampMinutes() {
        XCTAssertEqual(clampMinutes(0), 1)
        XCTAssertEqual(clampMinutes(-5), 1)
        XCTAssertEqual(clampMinutes(1), 1)
        XCTAssertEqual(clampMinutes(999), 999)
        XCTAssertEqual(clampMinutes(1000), 999)
        XCTAssertEqual(clampMinutes(45), 45)
    }
}
