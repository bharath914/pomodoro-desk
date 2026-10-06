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
    TimerEngine t(25);
    CHECK(t.state() == TimerState::Ready);
    CHECK(t.secsLeft(0) == 25 * 60);
    CHECK(!t.isRunning());
}

static void test_start_counts_down() {
    TimerEngine t(25);
    t.start(0);
    CHECK(t.isRunning());
    CHECK(t.secsLeft(10000) == 25 * 60 - 10);
}

static void test_pause_preserves_remaining() {
    TimerEngine t(25);
    t.start(0);
    t.pause(10000);
    CHECK(!t.isRunning());
    CHECK(t.secsLeft(10000) == 25 * 60 - 10);
    CHECK(t.secsLeft(999999) == 25 * 60 - 10);  // time passing while paused changes nothing
}

static void test_resume_continues_from_paused_remaining() {
    TimerEngine t(25);
    t.start(0);
    t.pause(10000);
    t.start(50000);  // resume at a later wall-clock time
    CHECK(t.secsLeft(50000) == 25 * 60 - 10);
    CHECK(t.secsLeft(55000) == 25 * 60 - 15);
}

static void test_toggle_round_trip() {
    TimerEngine t(25);
    t.toggle(0);        // start
    t.toggle(5000);     // pause
    int remainAfterFirstRound = t.secsLeft(5000);
    t.toggle(100000);   // resume
    t.toggle(100000);   // pause immediately, no time elapsed while running
    CHECK(t.secsLeft(100000) == remainAfterFirstRound);
}

static void test_add_minutes_while_running() {
    TimerEngine t(25);
    t.start(0);
    int before = t.secsLeft(0);
    t.addMinutes(5);
    CHECK(t.secsLeft(0) == before + 300);
    CHECK(t.isRunning());
}

static void test_add_minutes_while_paused() {
    TimerEngine t(25);
    t.addMinutes(10);
    CHECK(t.secsLeft(0) == 35 * 60);
    CHECK(!t.isRunning());
}

static void test_subtract_minutes_while_paused_clamps_at_zero() {
    TimerEngine t(5);
    t.addMinutes(-10);
    CHECK(t.secsLeft(0) == 0);
    CHECK(t.state() == TimerState::Done);
}

static void test_subtract_minutes_while_running() {
    TimerEngine t(25);
    t.start(0);
    t.addMinutes(-10);
    CHECK(t.secsLeft(0) == 15 * 60);
    CHECK(t.isRunning());
}

static void test_reset_reverts_to_full_duration() {
    TimerEngine t(25);
    t.start(0);
    t.update(10000);
    t.reset();
    CHECK(!t.isRunning());
    CHECK(t.secsLeft(10000) == 25 * 60);
}

static void test_set_duration_resets() {
    TimerEngine t(25);
    t.start(0);
    t.setDuration(45);
    CHECK(!t.isRunning());
    CHECK(t.secsLeft(0) == 45 * 60);
}

static void test_update_completes_exactly_once() {
    TimerEngine t(1);
    t.start(0);
    CHECK(t.update(30000) == false);
    CHECK(t.update(60000) == true);
    CHECK(t.state() == TimerState::Done);
    CHECK(!t.isRunning());
    CHECK(t.update(70000) == false);  // no re-trigger after completion
}

static void test_secs_left_never_negative() {
    TimerEngine t(1);
    t.start(0);
    CHECK(t.secsLeft(120000) == 0);  // past deadline, update() not called yet
}

static void test_done_can_be_revived_by_adding_time() {
    TimerEngine t(1);
    t.start(0);
    t.update(60000);
    CHECK(t.state() == TimerState::Done);
    t.addMinutes(5);
    CHECK(t.secsLeft(60000) > 0);
    CHECK(t.state() != TimerState::Done);
}

static void test_preset_durations() {
    int presets[] = { 25, 30, 45, 60, 120 };
    for (int minutes : presets) {
        TimerEngine t(minutes);
        CHECK(t.secsLeft(0) == minutes * 60);
    }
}

static void test_clamp_minutes() {
    CHECK(ClampMinutes(0) == 1);
    CHECK(ClampMinutes(-5) == 1);
    CHECK(ClampMinutes(1) == 1);
    CHECK(ClampMinutes(999) == 999);
    CHECK(ClampMinutes(1000) == 999);
    CHECK(ClampMinutes(45) == 45);
}

int main() {
    test_initial_state();
    test_start_counts_down();
    test_pause_preserves_remaining();
    test_resume_continues_from_paused_remaining();
    test_toggle_round_trip();
    test_add_minutes_while_running();
    test_add_minutes_while_paused();
    test_subtract_minutes_while_paused_clamps_at_zero();
    test_subtract_minutes_while_running();
    test_reset_reverts_to_full_duration();
    test_set_duration_resets();
    test_update_completes_exactly_once();
    test_secs_left_never_negative();
    test_done_can_be_revived_by_adding_time();
    test_preset_durations();
    test_clamp_minutes();

    std::printf("%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
