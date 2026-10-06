#include "../TimerEngine.h"
#include <cstdio>

static int g_fail = 0;
static int g_total = 0;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void test_initial_state() {
    TimerEngine t;
    CHECK(t.mode() == Mode::Focus);
    CHECK(t.state() == RunState::Ready);
    CHECK(t.secsLeft(0) == 25 * 60);
    CHECK(t.completedSessions() == 0);
    CHECK(t.cyclePosition() == 0);
    CHECK(!t.isRunning());
}

static void test_duration_for_mode() {
    CHECK(TimerEngine::durationForMode(Mode::Focus) == 25 * 60000);
    CHECK(TimerEngine::durationForMode(Mode::ShortBreak) == 5 * 60000);
    CHECK(TimerEngine::durationForMode(Mode::LongBreak) == 15 * 60000);
}

static void test_start_counts_down() {
    TimerEngine t;
    t.start(0);
    CHECK(t.isRunning());
    CHECK(t.secsLeft(10000) == 25 * 60 - 10);
}

static void test_pause_preserves_remaining() {
    TimerEngine t;
    t.start(0);
    t.pause(10000);
    CHECK(!t.isRunning());
    CHECK(t.secsLeft(10000) == 25 * 60 - 10);
    CHECK(t.secsLeft(999999) == 25 * 60 - 10);  // time passing while paused changes nothing
}

static void test_resume_continues_from_paused_remaining() {
    TimerEngine t;
    t.start(0);
    t.pause(10000);
    t.start(50000);  // resume at a later wall-clock time
    CHECK(t.secsLeft(50000) == 25 * 60 - 10);
    CHECK(t.secsLeft(55000) == 25 * 60 - 15);
}

static void test_toggle_round_trip() {
    TimerEngine t;
    t.toggle(0);        // start
    t.toggle(5000);     // pause
    int remainAfterFirstRound = t.secsLeft(5000);
    t.toggle(100000);   // resume
    t.toggle(100000);   // pause immediately, no time elapsed while running
    CHECK(t.secsLeft(100000) == remainAfterFirstRound);
}

static void test_select_mode_resets_paused() {
    TimerEngine t;
    t.start(0);
    t.selectMode(Mode::ShortBreak);
    CHECK(!t.isRunning());
    CHECK(t.mode() == Mode::ShortBreak);
    CHECK(t.secsLeft(0) == 5 * 60);
    CHECK(t.state() == RunState::Ready);
}

static void test_reset_reverts_to_current_mode_full_duration() {
    TimerEngine t;
    t.start(0);
    t.update(10000);
    t.reset();
    CHECK(!t.isRunning());
    CHECK(t.secsLeft(10000) == 25 * 60);
}

static void test_focus_completion_advances_to_short_break() {
    TimerEngine t;
    t.start(0);
    CHECK(t.update(25 * 60000) == true);
    CHECK(t.mode() == Mode::ShortBreak);
    CHECK(t.completedSessions() == 1);
    CHECK(t.cyclePosition() == 1);
    CHECK(!t.isRunning());
    CHECK(t.secsLeft(25 * 60000) == 5 * 60);
}

static void test_short_break_completion_returns_to_focus() {
    TimerEngine t;
    t.selectMode(Mode::ShortBreak);
    t.start(0);
    t.update(5 * 60000);
    CHECK(t.mode() == Mode::Focus);
    CHECK(t.cyclePosition() == 0);  // unaffected by a break completing (no focus session completed here)
    CHECK(t.secsLeft(5 * 60000) == 25 * 60);
}

static void test_fourth_focus_session_advances_to_long_break() {
    TimerEngine t;
    int64_t now = 0;
    for (int i = 0; i < 3; i++) {
        t.selectMode(Mode::Focus);
        t.start(now);
        now += 25 * 60000;
        t.update(now);
        CHECK(t.mode() == Mode::ShortBreak);
        t.selectMode(Mode::Focus);  // skip the break manually for this test
    }
    t.start(now);
    now += 25 * 60000;
    CHECK(t.update(now) == true);
    CHECK(t.mode() == Mode::LongBreak);
    CHECK(t.cyclePosition() == 4);
    CHECK(t.completedSessions() == 4);
}

static void test_long_break_completion_resets_cycle() {
    TimerEngine t;
    t.selectMode(Mode::LongBreak);
    t.start(0);
    t.update(15 * 60000);
    CHECK(t.mode() == Mode::Focus);
    CHECK(t.cyclePosition() == 0);
}

static void test_update_completes_exactly_once() {
    TimerEngine t;
    t.start(0);
    CHECK(t.update(25 * 60000 - 1000) == false);
    CHECK(t.update(25 * 60000) == true);
    CHECK(!t.isRunning());
    CHECK(t.update(25 * 60000 + 10000) == false);  // no re-trigger after completion
}

static void test_secs_left_never_negative() {
    TimerEngine t;
    t.start(0);
    CHECK(t.secsLeft(999 * 60000) == 0);  // way past deadline, update() not called yet
}

int main() {
    test_initial_state();
    test_duration_for_mode();
    test_start_counts_down();
    test_pause_preserves_remaining();
    test_resume_continues_from_paused_remaining();
    test_toggle_round_trip();
    test_select_mode_resets_paused();
    test_reset_reverts_to_current_mode_full_duration();
    test_focus_completion_advances_to_short_break();
    test_short_break_completion_returns_to_focus();
    test_fourth_focus_session_advances_to_long_break();
    test_long_break_completion_resets_cycle();
    test_update_completes_exactly_once();
    test_secs_left_never_negative();

    std::printf("%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
