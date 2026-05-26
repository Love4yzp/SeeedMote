#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../src/motion_gate.h"

static const struct motion_gate_config cfg = {
    .min_score_mg = 100,
    .cooldown_ms = 2000,
};

static void hardware_wake_emits_even_with_low_sample_score(void)
{
    struct motion_gate gate = {0};

    enum motion_gate_decision decision =
        motion_gate_update(&gate, &cfg, true, false, 0, 1000);

    assert(decision == MOTION_GATE_EMIT);
    assert(gate.moving);
}

static void cooldown_suppresses_repeats_but_does_not_require_inactivity(void)
{
    struct motion_gate gate = {0};

    assert(motion_gate_update(&gate, &cfg, true, false, 150, 1000) ==
           MOTION_GATE_EMIT);
    assert(motion_gate_update(&gate, &cfg, true, false, 150, 2500) ==
           MOTION_GATE_NONE);
    assert(motion_gate_update(&gate, &cfg, true, false, 150, 3100) ==
           MOTION_GATE_EMIT);
}

static void inactivity_marks_idle_without_blocking_next_wake(void)
{
    struct motion_gate gate = {0};

    assert(motion_gate_update(&gate, &cfg, true, false, 150, 1000) ==
           MOTION_GATE_EMIT);
    assert(motion_gate_update(&gate, &cfg, false, true, 0, 2000) ==
           MOTION_GATE_INACTIVITY);
    assert(!gate.moving);
    assert(motion_gate_update(&gate, &cfg, true, false, 150, 3100) ==
           MOTION_GATE_EMIT);
}

static void sample_score_is_fallback_when_source_bit_is_missing(void)
{
    struct motion_gate gate = {0};

    assert(motion_gate_update(&gate, &cfg, false, false, 120, 1000) ==
           MOTION_GATE_EMIT);
}

static void score_uses_largest_consecutive_axis_delta(void)
{
    const struct motion_sample samples[] = {
        { .x = 1000, .y = -1000, .z = 200 },
        { .x = 1100, .y = -900, .z = 200 },
        { .x = 1500, .y = -950, .z = -100 },
    };

    assert(motion_score_mg(samples, 1) == 0);
    assert(motion_score_mg(samples, 3) == 45);
}

int main(void)
{
    hardware_wake_emits_even_with_low_sample_score();
    cooldown_suppresses_repeats_but_does_not_require_inactivity();
    inactivity_marks_idle_without_blocking_next_wake();
    sample_score_is_fallback_when_source_bit_is_missing();
    score_uses_largest_consecutive_axis_delta();
    return 0;
}
