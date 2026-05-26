#include "motion_gate.h"

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

int32_t motion_score_mg(const struct motion_sample *samples, size_t count)
{
    int32_t max_delta = 0;

    if (samples == NULL || count < 2) {
        return 0;
    }

    for (size_t i = 1; i < count; i++) {
        int32_t dx = (int32_t)samples[i].x - samples[i - 1].x;
        int32_t dy = (int32_t)samples[i].y - samples[i - 1].y;
        int32_t dz = (int32_t)samples[i].z - samples[i - 1].z;
        int32_t delta = abs_i32(dx) + abs_i32(dy) + abs_i32(dz);

        if (delta > max_delta) {
            max_delta = delta;
        }
    }

    /* LSM6DSL/LSM6DS3TR-C at +/-2g is 0.061 mg per LSB. */
    return (max_delta * 61) / 1000;
}

enum motion_gate_decision motion_gate_update(struct motion_gate *gate,
                                             const struct motion_gate_config *cfg,
                                             bool hw_wake,
                                             bool hw_inactivity,
                                             int32_t score_mg,
                                             int64_t now_ms)
{
    bool score_wake;

    if (gate == NULL || cfg == NULL) {
        return MOTION_GATE_NONE;
    }

    if (hw_inactivity) {
        gate->moving = false;
        if (!hw_wake) {
            return MOTION_GATE_INACTIVITY;
        }
    }

    score_wake = score_mg >= cfg->min_score_mg;
    if (!hw_wake && !score_wake) {
        return MOTION_GATE_NONE;
    }

    gate->moving = true;
    if (gate->has_last_emit &&
        (now_ms - gate->last_emit_ms) < cfg->cooldown_ms) {
        return MOTION_GATE_NONE;
    }

    gate->has_last_emit = true;
    gate->last_emit_ms = now_ms;
    return MOTION_GATE_EMIT;
}
