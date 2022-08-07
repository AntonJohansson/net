#pragma once

#include "common.h"
#include <math.h>

enum input_type {
    INPUT_NULL,
    INPUT_MOVE_LEFT,
    INPUT_MOVE_RIGHT,
    INPUT_MOVE_UP,
    INPUT_MOVE_DOWN,
    INPUT_QUIT,
    INPUT_LAST,
};

struct input {
    bool active[INPUT_LAST];
};

struct player {
    f32 x;
    f32 y;
};

static inline void move(struct player *p, struct input *input, const f32 dt) {
    const f32 move_speed = 10.0f;
    f32 dir_x = 1.0f*(f32) input->active[INPUT_MOVE_RIGHT] - 1.0f*(f32) input->active[INPUT_MOVE_LEFT];
    f32 dir_y = 1.0f*(f32) input->active[INPUT_MOVE_DOWN]  - 1.0f*(f32) input->active[INPUT_MOVE_UP];
    f32 length2 = dir_x*dir_x + dir_y*dir_y;
    if (length2 > 0) {
        f32 length = sqrtf(length2);
        dir_x *= move_speed/length;
        dir_y *= move_speed/length;

        p->x += dir_x;
        p->y += dir_y;
    }
}
