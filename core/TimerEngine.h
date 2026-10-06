// Pure Pomodoro-cycle logic — no UI, no platform APIs, no system clock calls.
// Every method takes the current time explicitly so it is fully deterministic and testable.
#pragma once
#include <cstdint>

enum class Mode { Focus, ShortBreak, LongBreak };
enum class RunState { Ready, Running, Paused };

class TimerEngine {
public:
    TimerEngine();

    void selectMode(Mode m);  // switch modes directly (e.g. tapping a tab); resets, paused
    void start(int64_t nowMs);
    void pause(int64_t nowMs);
    void toggle(int64_t nowMs);
    void reset();  // back to the current mode's full duration, paused

    // Call periodically while running; returns true exactly once, the moment the countdown hits
    // zero. By the time it returns, mode() has already advanced to the next step of the cycle:
    // Focus -> ShortBreak (or LongBreak every 4th) -> Focus, each session counted and paused,
    // ready for the next Start press.
    bool update(int64_t nowMs);

    int64_t remainingMs(int64_t nowMs) const;
    int secsLeft(int64_t nowMs) const;
    RunState state() const;
    bool isRunning() const { return running_; }
    Mode mode() const { return mode_; }
    int completedSessions() const { return sessionCount_; }
    int cyclePosition() const { return cyclePos_; }  // 0..4 focus sessions completed since the last long break

    static int64_t durationForMode(Mode m);

private:
    Mode mode_ = Mode::Focus;
    int64_t remainMs_;
    int64_t endTime_ = 0;
    bool running_ = false;
    int sessionCount_ = 0;
    int cyclePos_ = 0;
};
