#pragma once

#include "common.h"
#include <math.h>
#include "v2.h"

#if defined(DRAW)
#include "draw.h"
#endif

enum input_type {
    INPUT_NULL,

    INPUT_MOVE_LEFT,
    INPUT_MOVE_RIGHT,
    INPUT_MOVE_UP,
    INPUT_MOVE_DOWN,

    INPUT_MOVE_DODGE,

    INPUT_SHOOT,

    INPUT_QUIT,
    INPUT_LAST,
};

struct input {
    f32 x;
    f32 y;
    bool active[INPUT_LAST];
};

enum player_state {
    PLAYER_STATE_DEFAULT = 0,
    PLAYER_STATE_SLIDING,
};

struct player {
    bool occupied;

    u64 id;

    v2 pos;
    v2 velocity;

    v2 dodge;
    v2 look;

    f32 time_left_in_dodge;
    f32 time_left_in_dodge_delay;

    f32 hue;

    f32 health;

    enum player_state state;
};

enum tiles {
    TILE_INVALID = 0,
    TILE_GRASS = ' ',
    TILE_STONE = '#',
};

struct map {
    const u8 *data;
    u32 width;
    u32 height;
    f32 tile_size;
    v2 origin;
};

#define MAX_PROJECTILES 64

struct game {
    struct map map;
    struct player players[MAX_CLIENTS];
};

static inline struct player *allocate_player(struct game *g, u64 id) {
    struct player *p = NULL;
    for (u32 i = 0; i < ARRLEN(g->players); ++i) {
        p = &g->players[i];
        if (!p->occupied)
            break;
    }

    assert(p != NULL);
    p->occupied = true;
    p->id = id;

    return p;
}

static inline struct player *lookup_player(struct game *g, u64 id) {
    for (u32 i = 0; i < ARRLEN(g->players); ++i) {
        struct player *p = &g->players[i];
        if (p->occupied && p->id == id)
            return p;
    }

    return NULL;
}

static inline u64 player_id() {
    static u64 id = 0;
    return id++;
}

static inline bool map_coord_in_bounds(const struct map *map, i32 i, i32 j) {
    return i >= 0 && i <= map->width &&
           j >= 0 && j <= map->height;
}

static inline void map_coord(const struct map *map, i32 *i, i32 *j, v2 at) {
    *i = (at.x - map->origin.x)/map->tile_size;
    *j = (at.y - map->origin.y)/map->tile_size;
}

static inline u8 map_at(const struct map *map, v2 at) {
    i32 i = 0;
    i32 j = 0;
    map_coord(map, &i, &j, at);
    if (!map_coord_in_bounds(map, i, j))
        return TILE_INVALID;

    return map->data[j*map->width + i];
}

static struct map map = {
    .data = (const u8 *) "################"
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
    u64 id0;
    u64 id1;
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

static struct collision_result collide_circle_circle(struct circle c0,
                                                     struct circle c1) {
    const f32 radius_sum = c0.radius + c1.radius;
    const v2 center_diff = v2sub(c1.pos, c0.pos);
    const f32 center_diff_len2 = v2len2(center_diff);

    struct collision_result result = {
        .colliding = false,
    };

    if (center_diff_len2 > radius_sum*radius_sum)
        return result;

    const f32 center_diff_len = sqrtf(center_diff_len2);
    const f32 overlap = radius_sum - center_diff_len;

    result.colliding = true;
    result.resolve = v2scale(overlap/center_diff_len, center_diff);

    return result;
}

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

static bool collide_ray_aabb(v2 pos, v2 dir, struct rectangle rect, v2 *res) {
    const f32 x0 = rect.pos.x;
    const f32 x1 = rect.pos.x + rect.width;
    const f32 y0 = rect.pos.y;
    const f32 y1 = rect.pos.y + rect.height;

    printf("pos: {%f, %f}\n", pos.x, pos.y);
    printf("dir: {%f, %f}\n", dir.x, dir.y);

    const f32 dist_x0 = fabsf(x0 - pos.x);
    const f32 dist_x1 = fabsf(x1 - pos.x);
    const f32 dist_y0 = fabsf(y0 - pos.y);
    const f32 dist_y1 = fabsf(y1 - pos.y);

    printf("%f,%f,%f,%f\n", x0, x1, y0, y1);
    printf("%f,%f,%f,%f\n", dist_x0, dist_x1, dist_y0, dist_y1);

    const f32 x = (dist_x0 < dist_x1) ? x0 : x1;
    const f32 y = (dist_y0 < dist_y1) ? y0 : y1;

    printf("%f, %f\n", x, y);

    if (v2dot((v2) {x-pos.x,y-pos.y}, dir) <= 0.0f)
        return false;

    const f32 hit_line_y = pos.y + (dir.y/dir.x)*fabsf(x - pos.x);
    const f32 hit_line_x = pos.x + (dir.x/dir.y)*fabsf(y - pos.y);

    const i32 hit_x = hit_line_x <= x1 && hit_line_x >= x0;
    const i32 hit_y = hit_line_y <= y1 && hit_line_y >= y0;
    const i32 hit = hit_x | hit_y;

    printf("checking collide %d, %d\n", hit_x, hit_y);
    printf("%f, %f\n", hit_line_x, hit_line_y);

    if (!hit)
        return false;

    printf("hit\n");

    if (hit_x)
        *res = (v2) {hit_line_x, y};
    else if (hit_y)
        *res = (v2) {x, hit_line_y};

    return true;
}

static void raycast(struct game *game, v2 pos, v2 dir, const f32 dt, v2 *res) {
    assert(f32_equal(v2len2(dir), 1.0f));
    for (i32 j = 0; j < game->map.height; ++j) {
        for (i32 i = 0; i < game->map.width; ++i) {
            const u8 tile = game->map.data[j*game->map.width + i];
            if (tile != TILE_STONE)
                continue;
            struct rectangle rect = {
                .pos = (v2) {
                    .x = game->map.origin.x + i*game->map.tile_size,
                    .y = game->map.origin.y + j*game->map.tile_size,
                },
                .width = game->map.tile_size,
                .height = game->map.tile_size,
            };

            if (collide_ray_aabb(pos, dir, rect, res))
                return;
        }
    }
    assert(false);
}

static const f32 dodge_delay_time = 1.0f;

static inline void move(struct game *game,
                        struct player *p,
                        struct input *input,
                        const f32 dt,
                        bool replaying) {
    const f32 move_acceleration = 0.5f/dt;
    const f32 max_move_speed = 5.0f;
    const f32 dodge_acceleration = 1.0f/dt;
    const f32 dodge_deceleration = 0.10f/dt;
    const f32 max_dodge_speed = 10.0f;
    const f32 dodge_time = 0.10f;

    p->look = v2normalize((v2) {input->x, input->y});

    if (p->time_left_in_dodge_delay > 0.0f) {
        p->time_left_in_dodge_delay -= dt;
        if (p->time_left_in_dodge_delay <= 0.0f)
            p->time_left_in_dodge_delay = 0.0f;
    }

    // Dodge state
    bool in_dodge = p->state == PLAYER_STATE_SLIDING;
    bool in_dodge_delay = p->time_left_in_dodge_delay > 0.0f;
    if (!in_dodge_delay && !in_dodge && input->active[INPUT_MOVE_DODGE]) {
        p->dodge = p->look;
        p->time_left_in_dodge = dodge_time;
        p->state = PLAYER_STATE_SLIDING;

        // If we have a velocity in any other direction than the dodge dir,
        // redirect it in the dodge dir
        const f32 speed = v2len(p->velocity);
        p->velocity = v2scale(speed, p->dodge);
    }

    // Dodge movement
    if (p->state == PLAYER_STATE_SLIDING) {
        if (p->time_left_in_dodge > 0.0f) {
            p->velocity = v2add(p->velocity, v2scale(dt*dodge_acceleration, p->dodge));
            const f32 speed = v2len(p->velocity);
            if (speed > max_dodge_speed) {
                p->velocity = v2scale(max_dodge_speed, v2normalize(p->velocity));
            }

            p->time_left_in_dodge -= dt;
            if (p->time_left_in_dodge <= 0.0f) {
                p->time_left_in_dodge = 0.0f;
            }
        } else {
            const v2 slowdown_dir = v2neg(v2normalize(p->velocity));
            const f32 speed = v2len(p->velocity);
            if (speed > 0.0f) {
                const f32 slowdown = fminf(speed, dt*dodge_deceleration);
                if (speed < dt*dodge_deceleration) {
                    p->state = PLAYER_STATE_DEFAULT;
                    p->time_left_in_dodge_delay = dodge_delay_time;
                }
                p->velocity = v2add(p->velocity, v2scale(slowdown, slowdown_dir));
            }
        }
    }

    // Process player inputs
    const f32 dx = 1.0f*(f32) input->active[INPUT_MOVE_RIGHT] - 1.0f*(f32) input->active[INPUT_MOVE_LEFT];
    const f32 dy = 1.0f*(f32) input->active[INPUT_MOVE_DOWN]  - 1.0f*(f32) input->active[INPUT_MOVE_UP];
    const v2 dv = {dx, dy};
    const f32 len2 = v2len2(dv);

    if (p->state == PLAYER_STATE_SLIDING && p->time_left_in_dodge == 0.0f) {
        const f32 speed = v2len(p->velocity);
        if (speed <= max_move_speed && len2 > 0.0f) {
            p->state = PLAYER_STATE_DEFAULT;
            p->time_left_in_dodge_delay = dodge_delay_time;
        }
    }

    // Player movement
    if (p->state != PLAYER_STATE_SLIDING) {
        if (len2 > 0.0f) {
            const f32 len = sqrtf(len2);

            p->velocity = v2add(p->velocity, v2scale(dt*move_acceleration/len, dv));
            const f32 speed = v2len(p->velocity);
            if (speed > max_move_speed) {
                p->velocity = v2scale(max_move_speed, v2normalize(p->velocity));
            }
        } else {
            const v2 slowdown_dir = v2neg(v2normalize(p->velocity));
            const f32 speed = v2len(p->velocity);
            if (speed > 0.0f) {
                const f32 slowdown = fminf(speed, dt*move_acceleration);
                p->velocity = v2add(p->velocity, v2scale(slowdown, slowdown_dir));
            }
        }
    }

    // "Integrate" velocity
    if (!v2iszero(p->velocity)) {
        p->pos = v2add(p->pos, v2scale(dt, p->velocity));
    }
}

static inline void collect_and_resolve_static_collisions(struct game *game) {
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

    for (u32 j = 0; j < ARRLEN(game->players); ++j) {
        struct player *p = &game->players[j];
        if (!p->occupied)
            continue;
        for (u32 i = 0; i < ARRLEN(tile_offsets); ++i) {
            const v2 at = v2add(p->pos, v2scale(game->map.tile_size, tile_offsets[i]));
            const u8 tile = map_at(&game->map, at);
            if (tile != TILE_STONE)
                continue;

            // TODO(anjo): Get collision shape from player instead!
            const f32 radius = 0.25f;
            struct collision_result result = collide_rect_circle((struct rectangle) {
                                                                 .pos = {floorf(at.x), floorf(at.y)},
                                                                 .width = game->map.tile_size,
                                                                 .height = game->map.tile_size,
                                                                 },
                                                                 (struct circle) {
                                                                 .pos = p->pos,
                                                                 .radius = radius,
                                                                 });
            if (!result.colliding)
                continue;

            if (v2iszero(result.resolve))
                continue;

            p->pos = v2add(p->pos, result.resolve);

            bool in_dodge = p->state == PLAYER_STATE_SLIDING;
            if (in_dodge) {
                f32 dot = v2dot(p->dodge, v2normalize(result.resolve));
                // resolve and dodge should be pointing in opposite directions.
                // If the dot product is <= -0.5f the relative direction between
                // the vectors should >= 90+45 deg, we choose -0.6f to be a bit
                // more lenient, feels a bit better.
                if (dot <= -0.6f) {
                    p->state = PLAYER_STATE_DEFAULT;
                    p->time_left_in_dodge = 0.0f;
                    p->time_left_in_dodge_delay = dodge_delay_time;
                }
            }
        }
    }
}

static inline void collect_dynamic_collisions(struct game *game,
                                             struct collision_result *results,
                                             u32 *num_results,
                                             u32 max_results) {
    *num_results = 0;
    for (i32 i = 0; i < ARRLEN(game->players); ++i) {
        struct player *p0 = &game->players[i];
        if (!p0->occupied)
            continue;
        for (i32 j = i+1; j < ARRLEN(game->players); ++j) {
            struct player *p1 = &game->players[j];
            if (!p1->occupied)
                continue;

            const f32 radius = 0.25f;
            struct collision_result result = collide_circle_circle((struct circle) {
                                                                       .pos = p0->pos,
                                                                       .radius = radius
                                                                   },
                                                                   (struct circle) {
                                                                       .pos = p1->pos,
                                                                       .radius = radius
                                                                   });
            if (!result.colliding)
                continue;

            if (v2iszero(result.resolve))
                continue;

            result.id0 = p0->id;
            result.id1 = p1->id;

            results[(*num_results)++] = result;
            if (*num_results >= max_results)
                return;
        }
    }
}

static inline void resolve_dynamic_collisions(struct game *game,
                                              struct collision_result *results,
                                              u32 num_results) {
    for (u32 i = 0; i < num_results; ++i) {
        struct collision_result *result = &results[i];
        struct player *p0 = lookup_player(game, result->id0);
        struct player *p1 = lookup_player(game, result->id1);
        p0->pos = v2add(p0->pos, v2scale(-0.5f, result->resolve));
        p1->pos = v2add(p1->pos, v2scale(+0.5f, result->resolve));
    }
}
