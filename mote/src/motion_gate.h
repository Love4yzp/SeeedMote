#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct motion_sample {
    int16_t x;
    int16_t y;
    int16_t z;
};

struct motion_gate_config {
    int32_t min_score_mg;
    int32_t cooldown_ms;
};

struct motion_gate {
    bool moving;
    bool has_last_emit;
    int64_t last_emit_ms;
};

enum motion_gate_decision {
    MOTION_GATE_NONE = 0,
    MOTION_GATE_EMIT,
    MOTION_GATE_INACTIVITY,
};

int32_t motion_score_mg(const struct motion_sample *samples, size_t count);

enum motion_gate_decision motion_gate_update(struct motion_gate *gate,
                                             const struct motion_gate_config *cfg,
                                             bool hw_wake,
                                             bool hw_inactivity,
                                             int32_t score_mg,
                                             int64_t now_ms);
