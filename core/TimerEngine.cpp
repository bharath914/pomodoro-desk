#include "TimerEngine.h"

int64_t TimerEngine::durationForMode(Mode m) {
    switch (m) {
    case Mode::Focus:      return 25LL * 60000;
    case Mode::ShortBreak: return 5LL * 60000;
    case Mode::LongBreak:  return 15LL * 60000;
    }
    return 25LL * 60000;
}

TimerEngine::TimerEngine() : remainMs_(durationForMode(mode_)) {}

void TimerEngine::selectMode(Mode m) {
    mode_ = m;
    remainMs_ = durationForMode(m);
    running_ = false;
}

void TimerEngine::reset() {
    remainMs_ = durationForMode(mode_);
    running_ = false;
}

void TimerEngine::start(int64_t nowMs) {
    if (remainMs_ <= 0) remainMs_ = durationForMode(mode_);
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

bool TimerEngine::update(int64_t nowMs) {
    if (!running_) return false;
    if (remainingMs(nowMs) > 0) return false;

    running_ = false;
    if (mode_ == Mode::Focus) {
        sessionCount_++;
        cyclePos_++;
        mode_ = (cyclePos_ >= 4) ? Mode::LongBreak : Mode::ShortBreak;
    } else {
        if (mode_ == Mode::LongBreak) cyclePos_ = 0;
        mode_ = Mode::Focus;
    }
    remainMs_ = durationForMode(mode_);
    return true;
}

int64_t TimerEngine::remainingMs(int64_t nowMs) const {
    int64_t ms = running_ ? (endTime_ - nowMs) : remainMs_;
    return ms < 0 ? 0 : ms;
}

int TimerEngine::secsLeft(int64_t nowMs) const {
    return static_cast<int>((remainingMs(nowMs) + 999) / 1000);
}

RunState TimerEngine::state() const {
    if (running_) return RunState::Running;
    if (remainMs_ < durationForMode(mode_)) return RunState::Paused;
    return RunState::Ready;
}
