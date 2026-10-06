#include "TimerEngine.h"

TimerEngine::TimerEngine(int durationMinutes)
    : durationMs_(static_cast<int64_t>(durationMinutes) * 60000), remainMs_(durationMs_) {}

void TimerEngine::setDuration(int minutes) {
    durationMs_ = static_cast<int64_t>(minutes) * 60000;
    reset();
}

void TimerEngine::reset() {
    remainMs_ = durationMs_;
    running_ = false;
}

void TimerEngine::start(int64_t nowMs) {
    if (remainMs_ <= 0) remainMs_ = durationMs_;
    endTime_ = nowMs + remainMs_;
    running_ = true;
}

void TimerEngine::pause(int64_t nowMs) {
    if (!running_) return;
    remainMs_ = endTime_ - nowMs;
    if (remainMs_ < 0) remainMs_ = 0;
    running_ = false;
}

void TimerEngine::toggle(int64_t nowMs) {
    if (running_) pause(nowMs);
    else start(nowMs);
}

void TimerEngine::addMinutes(int minutes) {
    int64_t add = static_cast<int64_t>(minutes) * 60000;
    if (running_) endTime_ += add;
    else remainMs_ += add;
}

bool TimerEngine::update(int64_t nowMs) {
    if (!running_) return false;
    if (remainingMs(nowMs) <= 0) {
        running_ = false;
        remainMs_ = 0;
        return true;
    }
    return false;
}

int64_t TimerEngine::remainingMs(int64_t nowMs) const {
    int64_t ms = running_ ? (endTime_ - nowMs) : remainMs_;
    return ms < 0 ? 0 : ms;
}

int TimerEngine::secsLeft(int64_t nowMs) const {
    return static_cast<int>((remainingMs(nowMs) + 999) / 1000);
}

TimerState TimerEngine::state() const {
    if (running_) return TimerState::Running;
    if (remainMs_ <= 0) return TimerState::Done;
    if (remainMs_ < durationMs_) return TimerState::Paused;
    return TimerState::Ready;
}

int ClampMinutes(int raw) {
    if (raw < 1) return 1;
    if (raw > 999) return 999;
    return raw;
}
