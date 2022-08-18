#pragma once

#include "common.h"

static inline u32 rotate_right(u32 value, u32 amount) {
    amount &= 31;
    u32 result = ((value >> amount) | (value << (32 - amount)));
    return result;
}

struct random_series_pcg {
    u64 state;
    u64 selector;
};

static inline struct random_series_pcg random_seed_pcg(u64 state, u64 selector) {
    return (struct random_series_pcg) {
        .state = state,
        .selector = (selector << 1) | 1,
    };
}

static inline u32 random_next_u32(struct random_series_pcg *series) {
    u64 state = series->state;
    state = state * 6364136223846793005ULL + series->selector;
    series->state = state;

    u32 pre_rotate = (u32)((state ^ (state >> 18)) >> 27);
    u32 result = rotate_right(pre_rotate, (i32)(state >> 59));

    return result;
}

static inline f32 random_next_unilateral(struct random_series_pcg *series) {
    return (f32) random_next_u32(series) / (f32) UINT32_MAX;
}

static inline f32 random_next_bilateral(struct random_series_pcg *series) {
    return 2.0f*random_next_unilateral(series) - 1.0f;
}
