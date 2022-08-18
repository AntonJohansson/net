#pragma once

#include "common.h"
#include <math.h>
#include "v2.h"

enum input_type {
    INPUT_NULL,

    INPUT_MOVE_LEFT,
    INPUT_MOVE_RIGHT,
    INPUT_MOVE_UP,
    INPUT_MOVE_DOWN,

    INPUT_MOVE_DODGE,

    INPUT_QUIT,
    INPUT_LAST,
};

struct input {
    f32 x;
    f32 y;
    bool active[INPUT_LAST];
};

struct player {
    v2 pos;
    v2 velocity;
    v2 acceleration;

    v2 dodge;
    v2 look;

    f32 time_left_in_dodge;
    f32 time_left_in_dodge_delay;
};

struct map {
    u8 *data;
    u32 width;
    u32 height;
    f32 tile_size;
    v2 origin;
};

enum tiles {
    TILE_INVALID = 0,
    TILE_GRASS = ' ',
    TILE_STONE = '#',
};

static inline u8 map_at(const struct map *map, v2 at) {
    i32 i = (at.x - map->origin.x)/map->tile_size;
    i32 j = (at.y - map->origin.y)/map->tile_size;
    bool in_map = i >= 0 && i <= map->width &&
                  j >= 0 && j <= map->height;
    if (!in_map)
        return TILE_INVALID;

    return map->data[j*map->width + i];
}

static struct map map = {
    .data = "################"
            "#              #"
            "# ####         #"
            "# #            #"
            "# #            #"
            "# #            #"
            "#              #"
            "#              #"
            "#              #"
            "#              #"
            "#              #"
            "#              #"
            "#        #     #"
            "#              #"
            "#              #"
            "################",
    .width = 16,
    .height = 16,
    .tile_size = 1.0f,
    .origin = {-8.0f, -8.0f},
};

static inline f32 clamp(f32 x, f32 a, f32 b) {
    return fminf(fmaxf(x, a), b);
}

struct collision_result {
    bool colliding;
    v2 resolve;
};

struct rectangle {
    v2 pos;
    f32 width;
    f32 height;
};

struct circle {
    v2 pos;
    f32 radius;
};

static struct collision_result collide_rect_circle(struct rectangle rect,
                                                   struct circle circle) {
    v2 nearest = {
        .x = clamp(circle.pos.x, rect.pos.x, rect.pos.x + rect.width),
        .y = clamp(circle.pos.y, rect.pos.y, rect.pos.y + rect.height),
    };

    nearest = v2sub(nearest, circle.pos);
    const f32 dist2 = v2len2(nearest);

    struct collision_result result = {
        .colliding = false,
    };
    if (circle.radius*circle.radius < dist2)
        return result;

    const f32 dist = sqrtf(dist2);
    result.colliding = true;
    result.resolve = v2scale(-(circle.radius-dist)/dist, nearest);

    return result;
}

static inline void move(const struct map *map,
                        struct player *p,
                        struct input *input, const f32 dt) {
    const f32 move_speed = 1.0f;
    const f32 dodge_speed = 2.0f;
    const f32 dodge_time = 1.0f;
    const f32 dodge_delay_time = 1.0f;

    p->look = v2normalize((v2) {input->x, input->y});

    if (p->time_left_in_dodge_delay > 0.0f) {
        p->time_left_in_dodge_delay -= dt;
        if (p->time_left_in_dodge_delay <= 0.0f)
            p->time_left_in_dodge_delay = 0.0f;
    }

    bool in_dodge = p->time_left_in_dodge > 0.0f;
    bool in_dodge_delay = p->time_left_in_dodge_delay > 0.0f;
    if (!in_dodge_delay && !in_dodge && input->active[INPUT_MOVE_DODGE]) {
        p->dodge = p->look;
        p->time_left_in_dodge = dodge_time;
    }

    bool has_moved = false;

    in_dodge = p->time_left_in_dodge > 0.0f;
    if (in_dodge) {
        //p->pos = v2add(p->pos, v2scale(dt*dodge_speed, p->dodge));
        has_moved = true;

        p->time_left_in_dodge -= dt;
        if (p->time_left_in_dodge <= 0.0f) {
            p->time_left_in_dodge = 0.0f;
            p->time_left_in_dodge_delay = dodge_delay_time;
        }
    }

    in_dodge = p->time_left_in_dodge > 0.0f;
    if (!in_dodge) {
        const f32 dx = 1.0f*(f32) input->active[INPUT_MOVE_RIGHT] - 1.0f*(f32) input->active[INPUT_MOVE_LEFT];
        const f32 dy = 1.0f*(f32) input->active[INPUT_MOVE_DOWN]  - 1.0f*(f32) input->active[INPUT_MOVE_UP];
        const v2 dv = {dx, dy};
        const f32 len2 = v2len2(dv);
        if (len2 > 0.0f) {
            const f32 len = sqrtf(len2);
            p->acceleration = v2add(p->acceleration, v2scale(0.1f/len, dv));
        }
    }

    if (!v2iszero(p->velocity)) {
        const v2 vel_dir = v2normalize(p->velocity);
        const f32 acc_in_vel_dir = v2dot(vel_dir, p->acceleration);
        if (acc_in_vel_dir > 0.0f) {
            const f32 acc = fminf(acc_in_vel_dir, -0.05f);
            p->acceleration = v2add(p->acceleration, v2scale(acc, vel_dir));
        }
    }

    if (!v2iszero(p->acceleration)) {
        p->velocity = v2add(p->velocity, v2scale(dt, p->acceleration));
    }

    if (!v2iszero(p->velocity)) {
        p->pos = v2add(p->pos, v2scale(dt, p->velocity));
        has_moved = true;
    }

    if (has_moved) {
        const v2 tile_offsets[8] = {
            {+1,  0},
            {+1, -1},
            { 0, -1},
            {-1, -1},
            {-1,  0},
            {-1, +1},
            { 0, +1},
            {+1, +1},
        };

        for (i32 i = 0; i < ARRLEN(tile_offsets); ++i) {
            const v2 at = v2add(p->pos, v2scale(map->tile_size, tile_offsets[i]));
            const u8 tile = map_at(map, at);
            if (tile != TILE_STONE)
                continue;

            struct collision_result result = collide_rect_circle((struct rectangle) {
                                                                    .pos = {floorf(at.x), floorf(at.y)},
                                                                    .width = map->tile_size,
                                                                    .height = map->tile_size,
                                                                 },
                                                                 (struct circle) {
                                                                    .pos = p->pos,
                                                                    .radius = 0.25f,
                                                                 });
            if (!result.colliding)
                continue;

            p->pos = v2add(p->pos, result.resolve);

            if (in_dodge) {
                f32 dot = v2dot(p->dodge, v2normalize(result.resolve));
                // resolve and dodge should be pointing in opposite directions.
                // If the dot product is <= -0.5f the relative direction between
                // the vectors should >= 90+45 deg, we choose -0.6f to be a bit
                // more lenient, feels a bit better.
                if (dot <= -0.6f) {
                    p->time_left_in_dodge = 0.0f;
                    p->time_left_in_dodge_delay = dodge_delay_time;
                }
            }

            {
                f32 dot = v2dot(v2normalize(p->acceleration), v2normalize(result.resolve));
                // resolve and dodge should be pointing in opposite directions.
                // If the dot product is <= -0.5f the relative direction between
                // the vectors should >= 90+45 deg, we choose -0.6f to be a bit
                // more lenient, feels a bit better.
                if (dot <= -0.6f) {
                    p->acceleration = (v2) {0, 0};
                    p->velocity = (v2) {0, 0};
                }
            }
        }
    }
}
