// Pure countdown-timer logic — no UI, no platform APIs, no system clock calls.
// Every method takes the current time explicitly so it is fully deterministic and testable.
#pragma once
#include <cstdint>

enum class TimerState { Ready, Running, Paused, Done };

class TimerEngine {
public:
    explicit TimerEngine(int durationMinutes = 25);

    void setDuration(int minutes);
    void reset();
    void start(int64_t nowMs);
    void pause(int64_t nowMs);
    void toggle(int64_t nowMs);
    void addMinutes(int minutes);

    // Call periodically while running; returns true exactly once, the moment the countdown hits zero.
    bool update(int64_t nowMs);

    int64_t remainingMs(int64_t nowMs) const;
    int secsLeft(int64_t nowMs) const;
    TimerState state() const;
    bool isRunning() const { return running_; }
    int64_t durationMs() const { return durationMs_; }

private:
    int64_t durationMs_;
    int64_t remainMs_;
    int64_t endTime_ = 0;
    bool running_ = false;
};

// Clamp a user-entered minute value to the valid [1, 999] range.
int ClampMinutes(int raw);
