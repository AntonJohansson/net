#pragma once

#include <math.h>
#include "common.h"

typedef struct v2 v2;
struct v2 {
    f32 x;
    f32 y;
};

static inline bool v2equal(v2 a, v2 b) {
    return f32_equal(a.x, b.x) && f32_equal(a.y, b.y);
}

static inline bool v2iszero(v2 v) {
    return v2equal(v, (v2) {0, 0});
}

static inline v2 v2add(v2 a, v2 b) {
    return (v2) {
        .x = a.x + b.x,
        .y = a.y + b.y,
    };
}

static inline v2 v2sub(v2 a, v2 b) {
    return (v2) {
        .x = a.x - b.x,
        .y = a.y - b.y,
    };
}

static inline v2 v2neg(v2 v) {
    return (v2) {
        .x = -v.x,
        .y = -v.y,
    };
}

static inline v2 v2scale(f32 f, v2 v) {
    return (v2) {
        .x = f*v.x,
        .y = f*v.y,
    };
}

static inline v2 v2div(v2 v, f32 f) {
    return (v2) {
        .x = v.x/f,
        .y = v.y/f,
    };
}

static inline f32 v2dot(v2 a, v2 b) {
    return a.x*b.x + a.y*b.y;
}

static inline f32 v2len2(v2 v) {
    return v2dot(v, v);
}

static inline f32 v2len(v2 v) {
    return sqrtf(v2len2(v));
}

static inline v2 v2normalize(v2 v) {
    return v2div(v, v2len(v));
}

static inline v2 v2reflect(v2 v, v2 r) {
    assert(f32_equal(v2len2(r), 1.0f));
    const f32 amount_in_dir = v2dot(v, r);
    return v2add(v, v2scale(-2.0f*amount_in_dir, r));
}
