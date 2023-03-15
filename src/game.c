#include "game.h"
#include <math.h>
#include <float.h>

//
// Projectiles/Hitscan
//

static inline void fire_nade_projectile(struct game *game, struct player *shooter) {
    struct raycast_result res = raycast_map(game, shooter->pos, shooter->look);
    assert(res.hit);

    struct nade_projectile nade = {
        .player_id_from = shooter->id,
        .dir = shooter->look,
        .start_pos = shooter->pos,
        .pos = shooter->pos,
        .vel = 4.0f*shooter->nade_distance,
        .impact = res.impact,
        .impact_distance = res.distance,
        .impact_normal = res.normal,
        .time_left = nade_explode_time,
    };

    ListInsert(game->nade_list, nade);
    ListInsert(game->new_nade_list, nade);
}

static inline void fire_hitscan_projectile(struct game *game, struct player *shooter) {
    struct player *hit_player = NULL;
    struct raycast_result map_res = raycast_map(game, shooter->pos, shooter->look);
    struct raycast_result player_res = raycast_players(game, shooter->pos, shooter->look, &hit_player);

    struct hitscan_projectile hitscan = {
        .player_id_from = shooter->id,
        .player_id_to = (hit_player != NULL) ? hit_player->id : HASH_MAP_INVALID_HASH,
        .dir = shooter->look,
        .pos = shooter->pos,
        .impact = (hit_player != NULL) ? player_res.impact : map_res.impact,
        .time_left = sniper_trail_time,
    };
    ListInsert(game->hitscan_list, hitscan);
    ListInsert(game->new_hitscan_list, hitscan);

    // Simplifty conditions?
    // @OPTIMIZE
    if (player_res.hit && (!map_res.hit || player_res.distance < map_res.distance)) {
        // Hit player
        struct damage_entry damage = {
            .damage = 100.0f,
            .player_id = hit_player->id,
            };
        ListInsert(game->damage_list, damage);
    } else if (map_res.hit && (!player_res.hit || player_res.distance > map_res.distance)) {
        // Hit wall
    }
}

//
// Update functions
//

void update_player(struct game *game, struct player *p, struct input *input, const f32 dt) {
    f32 active_max_move_speed = max_move_speed;
    if (p->sniper_zoom > 0.0f) {
        active_max_move_speed -= 2.5f * p->sniper_zoom;
    }

    p->look = v2normalize(input->look);

    // Update dodge delay
    if (p->time_left_in_dodge_delay > 0.0f) {
        p->time_left_in_dodge_delay -= dt;
        if (p->time_left_in_dodge_delay <= 0.0f)
            p->time_left_in_dodge_delay = 0.0f;
    }

    // Update weapon cooldowns
    for (u32 i = 0; i < ARRLEN(p->weapons); ++i) {
        if (p->time_left_in_weapon_cooldown[i] > 0.0f) {
            p->time_left_in_weapon_cooldown[i] -= dt;
            if (p->time_left_in_weapon_cooldown[i] <= 0.0f)
                p->time_left_in_weapon_cooldown[i] = 0.0f;
        }
    }

    // Dodge state
    bool in_dodge = p->state == PLAYER_STATE_SLIDING;
    bool in_dodge_delay = p->time_left_in_dodge_delay > 0.0f;
    if (!in_dodge_delay && !in_dodge && input->active[INPUT_MOVE_DODGE]) {
        p->dodge = p->look;
        p->time_left_in_dodge = dodge_time;
        p->state = PLAYER_STATE_SLIDING;
        ListInsert(game->sound_list, ((struct spatial_sound){p->id, SOUND_PLAYER_SLIDE, p->pos}));

        // If we have a velocity in any other direction than the dodge dir,
        // redirect it in the dodge dir
        const f32 speed = v2len(p->velocity);
        p->velocity = v2scale(speed, p->dodge);
    }

    // Shoot state
    if (input->active[INPUT_SWITCH_WEAPON]) {
        p->current_weapon = (p->current_weapon + 1) % ARRLEN(p->weapons);
        ListInsert(game->sound_list, ((struct spatial_sound){p->id, SOUND_WEAPON_SWITCH, p->pos}));
    }

    if (p->weapons[p->current_weapon] == PLAYER_WEAPON_SNIPER && input->active[INPUT_ZOOM]) {
        const f32 max_zoom = 1.0f;
        if (p->sniper_zoom < max_zoom)
            p->sniper_zoom += 0.01f;
    } else if (p->sniper_zoom > 0.0f) {
        p->sniper_zoom -= 0.01f;
        if (p->sniper_zoom <= 0.0f)
            p->sniper_zoom = 0.0f;
    }

    if (p->weapons[p->current_weapon] != PLAYER_WEAPON_NADE && p->nade_distance > 0.0f) {
        p->nade_distance = 0.0f;
    }

    bool can_fire = p->time_left_in_weapon_cooldown[p->current_weapon] <= 0.0f;

    if (can_fire && p->weapons[p->current_weapon] == PLAYER_WEAPON_SNIPER && input->active[INPUT_SHOOT_PRESSED]) {
        p->time_left_in_weapon_cooldown[p->current_weapon] = weapon_cooldown;
        fire_hitscan_projectile(game, p);
    }

    if (can_fire && p->weapons[p->current_weapon] == PLAYER_WEAPON_NADE && input->active[INPUT_SHOOT_HELD]) {
        const f32 max_distance = 3.0f;
        if (p->nade_distance < max_distance)
            p->nade_distance += 0.1f;
    }

    if (can_fire && p->weapons[p->current_weapon] == PLAYER_WEAPON_NADE && input->active[INPUT_SHOOT_RELEASED]) {
        p->time_left_in_weapon_cooldown[p->current_weapon] = weapon_cooldown;
        fire_nade_projectile(game, p);
        p->nade_distance = 0.0f;
    }

    // Process player inputs
    const f32 dx = 1.0f*(f32) input->active[INPUT_MOVE_RIGHT] - 1.0f*(f32) input->active[INPUT_MOVE_LEFT];
    const f32 dy = 1.0f*(f32) input->active[INPUT_MOVE_DOWN]  - 1.0f*(f32) input->active[INPUT_MOVE_UP];
    const v2 dv = {dx, dy};
    const f32 len2 = v2len2(dv);

    // Slide movement
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

            // Allow movement at the end of the dodge
            if (len2 > 0.0f) {
                const f32 len = sqrtf(len2);
                p->velocity = v2add(p->velocity, v2scale(dt*move_acceleration/len, dv));
            }
            const f32 new_speed = v2len(p->velocity);
            if (new_speed > speed) {
                p->velocity = v2scale(speed, v2normalize(p->velocity));
            }

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

    // Leave slide state
    if (p->state == PLAYER_STATE_SLIDING && p->time_left_in_dodge == 0.0f) {
        const f32 speed = v2len(p->velocity);
        if (speed <= active_max_move_speed && len2 > 0.0f) {
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
            if (speed > active_max_move_speed) {
                p->velocity = v2scale(active_max_move_speed, v2normalize(p->velocity));
            }

            p->step_delay -= dt;
            f32 new_step_delay = f32_min(step_delay / speed, step_delay);
            if (new_step_delay < p->step_delay)
                p->step_delay = new_step_delay;
            if (p->step_delay < 0.0f) {
                p->step_delay = new_step_delay;

                f32 step_offset = 0.25f * ((p->step_left_side) ? 1.0f : -1.0f);
                v2 orthogonal_dir = {-p->look.y, p->look.x};
                v2 pos = v2add(p->pos, v2scale(step_offset, orthogonal_dir));
                ListInsert(game->sound_list, ((struct spatial_sound){p->id, SOUND_STEP, pos}));
                ListInsert(game->step_list, ((struct step){p->id, pos, 5.0f}));
                p->step_left_side = !p->step_left_side;
            }
        } else {
            const v2 slowdown_dir = v2neg(v2normalize(p->velocity));
            const f32 speed = v2len(p->velocity);
            if (speed > 0.0f) {
                const f32 slowdown = fminf(speed, dt*move_acceleration);
                p->velocity = v2add(p->velocity, v2scale(slowdown, slowdown_dir));
            } else {
                p->step_delay = 0.0f;
            }
        }
    }

    // "Integrate" velocity
    if (!v2iszero(p->velocity)) {
        p->pos = v2add(p->pos, v2scale(dt, p->velocity));
    }
}

void update_projectiles(struct game *game, const f32 dt) {
    ForEachList(game->hitscan_list, struct hitscan_projectile, hitscan) {
        // Emit sounds if the projectile is new
        if (f32_equal(hitscan->time_left, sniper_trail_time)) {
            ListInsert(game->sound_list, ((struct spatial_sound){hitscan->player_id_from, SOUND_SNIPER_FIRE, hitscan->pos}));
        }

        // update timer
        hitscan->time_left -= dt;
        if (hitscan->time_left < 0.0f)
            hitscan->time_left = 0.0f;

        // delete "dead" projectile
        if (hitscan->time_left <= 0.0f)
            ListTagRemovePtr(game->hitscan_list, hitscan);
    }
    ListRemoveTaggedItems(game->hitscan_list);

    ForEachList(game->nade_list, struct nade_projectile, nade) {

        // NOTE(anjo): Maybe just use velocity vector to start with
        // @OPTIMIZATION
        v2 vel = v2scale(nade->vel, nade->dir);

        const v2 slowdown_dir = v2neg(v2normalize(vel));
        const f32 speed = nade->vel;
        if (speed > 0.0f) {
            const f32 slowdown = fminf(speed, dt*nade_deceleration);
            vel = v2add(vel, v2scale(slowdown, slowdown_dir));
            nade->vel = v2len(vel);
        }

        nade->pos = v2add(nade->pos, v2scale(dt, vel));

        f32 dist = v2len2(v2sub(nade->pos, nade->start_pos));
        if (dist > nade->impact_distance*nade->impact_distance) {
            // We've hit a wall, reflect the nade
            nade->dir = v2reflect(nade->dir, nade->impact_normal);
            nade->start_pos = v2add(nade->impact, v2scale(0.1f, nade->impact_normal));

            struct raycast_result res = raycast_map(game, nade->start_pos, nade->dir);
            nade->pos = nade->start_pos;
            nade->impact = res.impact;
            nade->impact_distance = res.distance;

            ListInsert(game->sound_list, ((struct spatial_sound){nade->player_id_from, SOUND_NADE_DOINK, nade->pos}));
        }

        nade->time_left -= dt;
        if ((u32)(nade->time_left / dt) % 64 == 0)
            ListInsert(game->sound_list, ((struct spatial_sound){nade->player_id_from, SOUND_NADE_BEEP, nade->pos}));

        if (nade->time_left < 0.0f) {
            nade->time_left = 0.0f;

            ListInsert(game->sound_list, ((struct spatial_sound){nade->player_id_from, SOUND_NADE_EXPLOSION, nade->pos}));

            // Explode on time out
            struct explosion e = {
                .player_id_from = nade->player_id_from,
                .pos = nade->pos,
                .radius = 2.0f,
                .time_left = 1.0f,
            };
            ListInsert(game->explosion_list, e);

            HashMapForEach(game->player_map, struct player, p) {
                if (!HashMapExists(game->player_map, p))
                    continue;
                struct collision_result result = collide_circle_circle((struct circle) {
                                                                            .pos = p->pos,
                                                                            .radius = 0.25f,
                                                                       },
                                                                       (struct circle) {
                                                                            .pos = e.pos,
                                                                            .radius = e.radius,
                                                                       });

                if (result.colliding) {
                    v2 diff = v2sub(p->pos, e.pos);
                    const f32 dist_to_player = v2len(diff);
                    v2 dir = v2div(diff, dist_to_player);
                    struct raycast_result res = raycast_map(game, e.pos, dir);

                    if (res.distance >= dist_to_player) {
                        struct damage_entry damage = {
                            .damage = 100.0f,
                            .player_id = p->id,
                        };
                        ListInsert(game->damage_list, damage);
                    }
                }
            }

            ListTagRemovePtr(game->nade_list, nade);
        }
    }
    ListRemoveTaggedItems(game->nade_list);

    ForEachList(game->explosion_list, struct explosion, e) {
        e->time_left -= dt;
        if (e->time_left < 0.0f) {
            e->time_left = 0.0f;
            ListTagRemovePtr(game->explosion_list, e);
        }
    }
    ListRemoveTaggedItems(game->explosion_list);

    // I guess this is not a "projectile", but neither is an
    // explosion
    ForEachList(game->step_list, struct step, s) {
        s->time_left -= dt;
        if (s->time_left < 0.0f) {
            s->time_left = 0.0f;
            ListTagRemovePtr(game->step_list, s);
        }
    }
    ListRemoveTaggedItems(game->step_list);
}

//
// Collision detection
//

struct collision_result collide_circle_circle(struct circle c0, struct circle c1) {
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

struct collision_result collide_aabb_circle(struct aabb aabb, struct circle circle) {
    v2 nearest = {
        .x = f32_clamp(circle.pos.x, aabb.pos.x, aabb.pos.x + aabb.width),
        .y = f32_clamp(circle.pos.y, aabb.pos.y, aabb.pos.y + aabb.height),
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

struct raycast_result collide_ray_circle(v2 pos, v2 dir, struct circle circle) {
    struct raycast_result res = {0};

    v2 m = v2sub(pos, circle.pos);
    f32 c = v2len2(m) - circle.radius*circle.radius;
    f32 b = v2dot(m, dir);
    f32 disc = b*b - c;
    if (disc < 0.0f)
        return res;

    f32 t = -b - sqrtf(disc);
    if (t < 0)
        return res;

    res.hit = true;
    res.impact = v2add(pos, v2scale(t, dir));
    res.distance = t;
    return res;
}

struct raycast_result collide_ray_aabb(v2 pos, v2 dir, struct aabb aabb) {
    struct raycast_result res = {0};

    const f32 x0 = aabb.pos.x;
    const f32 x1 = aabb.pos.x + aabb.width;
    const f32 y0 = aabb.pos.y;
    const f32 y1 = aabb.pos.y + aabb.height;

    const f32 dist_x0 = fabsf(x0 - pos.x);
    const f32 dist_x1 = fabsf(x1 - pos.x);
    const f32 dist_y0 = fabsf(y0 - pos.y);
    const f32 dist_y1 = fabsf(y1 - pos.y);

    const f32 x = (dist_x0 < dist_x1) ? x0 : x1;
    const f32 y = (dist_y0 < dist_y1) ? y0 : y1;

    if (v2dot((v2) {x-pos.x,y-pos.y}, dir) <= 0.0f)
        return res;

    f32 kx = 0;
    f32 ky = 0;

    // We can simplify this
    // @OPTIMIZE
    if        (dir.y < 0 && dir.x > 0) {
        kx = -dir.x/dir.y;
        ky =  dir.y/dir.x;
    } else if (dir.y < 0 && dir.x < 0) {
        kx = -dir.x/dir.y;
        ky = -dir.y/dir.x;
    } else if (dir.y > 0 && dir.x < 0) {
        kx =  dir.x/dir.y;
        ky = -dir.y/dir.x;
    } else if (dir.y > 0 && dir.x > 0) {
        kx =  dir.x/dir.y;
        ky =  dir.y/dir.x;
    }

    const f32 hit_line_y = pos.y + ky*fabsf(x - pos.x);
    const f32 hit_line_x = pos.x + kx*fabsf(y - pos.y);

    const i32 hit_x = !f32_equal(dir.y, 0.0f) && hit_line_x <= x1 && hit_line_x >= x0;
    const i32 hit_y = !f32_equal(dir.x, 0.0f) && hit_line_y <= y1 && hit_line_y >= y0;
    const i32 hit = hit_x | hit_y;

    if (!hit)
        return res;

    // We can simplify this
    // @OPTIMIZE
    v2 vx = {hit_line_x, y};
    v2 vy = (v2) {x, hit_line_y};

    const f32 normal_sign_x = (v2dot((v2){0,1}, dir) > 0.0f) ? -1.0f : 1.0f;
    const f32 normal_sign_y = (v2dot((v2){1,0}, dir) > 0.0f) ? -1.0f : 1.0f;

    // Checking dot product between directions here is very inefficient
    // @OPTIMIZE
    if (hit_x && hit_y) {
        bool x_is_closer = v2len2(v2sub(vx,pos)) < v2len2(v2sub(vy,pos));
        v2 c = (x_is_closer) ? vx : vy;
        if (!f32_equal(v2dot(v2normalize(v2sub(c,pos)), dir), 1.0f))
            return res;
        res.hit = true;
        res.impact = c;
        res.normal = (x_is_closer) ? v2scale(normal_sign_x, (v2){0,1}) :
                                     v2scale(normal_sign_y, (v2){1,0});

        res.distance = v2len(v2sub(c,pos));
    } else if (hit_x) {
        if (!f32_equal(v2dot(v2normalize(v2sub(vx,pos)), dir), 1.0f))
            return res;
        res.hit = true;
        res.impact = vx;
        res.normal = v2scale(normal_sign_x, (v2){0,1});
        res.distance = v2len(v2sub(vx,pos));
    } else if (hit_y) {
        if (!f32_equal(v2dot(v2normalize(v2sub(vy,pos)), dir), 1.0f))
            return res;
        res.hit = true;
        res.impact = vy;
        res.normal = v2scale(normal_sign_y, (v2){1,0});
        res.distance = v2len(v2sub(vy,pos));
    }

    return res;
}

struct raycast_result raycast_map(struct game *game, v2 pos, v2 dir) {
    assert(f32_equal(v2len2(dir), 1.0f));

    struct raycast_result smallest_res = {
        .distance = FLT_MAX,
    };

    for (i32 j = 0; j < game->map.height; ++j) {
        for (i32 i = 0; i < game->map.width; ++i) {
            const u8 tile = game->map.data[j*game->map.width + i];
            if (tile != TILE_STONE)
                continue;
            struct aabb aabb = {
                .pos = (v2) {
                    .x = game->map.origin.x + i*game->map.tile_size,
                    .y = game->map.origin.y + j*game->map.tile_size,
                },
                .width = game->map.tile_size,
                .height = game->map.tile_size,
            };

            struct raycast_result res = collide_ray_aabb(pos, dir, aabb);
            if (res.hit && res.distance < smallest_res.distance) {
                smallest_res = res;
            }
        }
    }

    smallest_res.hit = smallest_res.distance != FLT_MAX;
    return smallest_res;
}

struct raycast_result raycast_players(struct game *game, v2 pos, v2 dir, struct player **hit_player) {
    assert(f32_equal(v2len2(dir), 1.0f));

    struct raycast_result smallest_res = {
        .distance = FLT_MAX,
    };

    HashMapForEach(game->player_map, struct player, p) {
        if (!HashMapExists(game->player_map, p))
            continue;
        struct circle c = {
            .pos = p->pos,
            .radius = 0.25f,
        };

        struct raycast_result res = collide_ray_circle(pos, dir, c);
        if (res.hit && res.distance < smallest_res.distance) {
            smallest_res = res;
            *hit_player = p;
        }
    }

    smallest_res.hit = smallest_res.distance != FLT_MAX;
    return smallest_res;
}

void collect_and_resolve_static_collisions_for_player(struct game *game, struct player *p) {
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

    for (u32 i = 0; i < ARRLEN(tile_offsets); ++i) {
        const v2 at = v2add(p->pos, v2scale(game->map.tile_size, tile_offsets[i]));
        const u8 tile = map_at(&game->map, at);
        if (tile != TILE_STONE)
            continue;

        // TODO(anjo): Get collision shape from player instead!
        const f32 radius = 0.25f;
        struct collision_result result = collide_aabb_circle((struct aabb) {
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

void collect_and_resolve_static_collisions(struct game *game) {
    HashMapForEach(game->player_map, struct player, p) {
        if (!HashMapExists(game->player_map, p))
            continue;
        collect_and_resolve_static_collisions_for_player(game, p);
    }
}

void collect_dynamic_collisions(struct game *game, struct collision_result *results, u32 *num_results, u32 max_results) {
    // Condense hash map to array, this will help with quick iteration
    //struct player *players[game->player_map.num_items];
    //size_t index = 0;
    //HashMapForEach(game->player_map, struct player, p) {
    //    if (!HashMapExists(game->player_map, p))
    //        continue;
    //    players[index++] = p;
    //}

    //*num_results = 0;
    //for (i32 i = 0; i < game->player_map.num_items; ++i) {
    //    struct player *p0 = players[i];
    //    for (i32 j = i+1; j < game->player_map.num_items; ++j) {
    //        struct player *p1 = players[j];

    //        const f32 radius = 0.25f;
    //        struct collision_result result = collide_circle_circle((struct circle) {
    //                                                                   .pos = p0->pos,
    //                                                                   .radius = radius
    //                                                               },
    //                                                               (struct circle) {
    //                                                                   .pos = p1->pos,
    //                                                                   .radius = radius
    //                                                               });
    //        if (!result.colliding)
    //            continue;

    //        if (v2iszero(result.resolve))
    //            continue;

    //        result.id0 = p0->id;
    //        result.id1 = p1->id;

    //        results[(*num_results)++] = result;
    //        if (*num_results >= max_results)
    //            return;
    //    }
    //}
}

void resolve_dynamic_collisions(struct game *game, struct collision_result *results, u32 num_results) {
    for (u32 i = 0; i < num_results; ++i) {
        struct collision_result *result = &results[i];
        struct player *p0 = NULL;
        struct player *p1 = NULL;
        HashMapLookup(game->player_map, result->id0, p0);
        HashMapLookup(game->player_map, result->id1, p1);
        p0->pos = v2add(p0->pos, v2scale(-0.5f, result->resolve));
        p1->pos = v2add(p1->pos, v2scale(+0.5f, result->resolve));
    }
}
