#pragma once

#include "common.h"
#include "audio.h"
#include "v2.h"

#include <string.h>
#include <assert.h>

// Game related constants
static const f32 nade_deceleration = 10.0f;

static const f32 move_acceleration = 50.0f;
static const f32 max_move_speed = 5.0f;
static const f32 step_delay = 1.0f;

static const f32 dodge_acceleration = 100.0f;
static const f32 dodge_deceleration = 10.f;
static const f32 max_dodge_speed = 10.0f;
static const f32 dodge_time = 0.20f;
static const f32 dodge_delay_time = 1.0f;

static const f32 weapon_sniper_cooldown = 1.0f;
static const f32 weapon_nade_cooldown = 3.0f;

static const f32 sniper_trail_time = 1.0f;

static const f32 nade_explode_time = 2.0f;

//
// Input
//

enum input_type {
    INPUT_NULL = 0,

    INPUT_MUTE,

    INPUT_MOVE_LEFT,
    INPUT_MOVE_RIGHT,
    INPUT_MOVE_UP,
    INPUT_MOVE_DOWN,

    INPUT_MOVE_DODGE,

    INPUT_SHOOT_PRESSED,
    INPUT_SHOOT_HELD,
    INPUT_SHOOT_RELEASED,
    INPUT_SWITCH_WEAPON,
    INPUT_ZOOM,

    INPUT_FULLSCREEN,

    INPUT_QUIT,
    INPUT_LAST,
};

Pack(struct input {
    v2 look;
    bool active[INPUT_LAST];
});

//
// Player
//

enum player_state {
    PLAYER_STATE_DEFAULT = 0,
    PLAYER_STATE_SLIDING,
};

enum player_weapon {
    PLAYER_WEAPON_SNIPER = 0,
    PLAYER_WEAPON_NADE,
};

typedef u64 PlayerId;

Pack(struct player {
    PlayerId id;

    v2 pos;
    v2 velocity;

    v2 dodge;
    v2 look;

    f32 step_delay;
    bool step_left_side;

    f32 time_left_in_dodge;
    f32 time_left_in_dodge_delay;

    f32 hue;

    f32 health;

    f32 time_left_in_weapon_cooldown[2];
    enum player_weapon weapons[2];
    u32 current_weapon;

    f32 nade_distance;

    f32 sniper_zoom;

    enum player_state state;
});

//
// Map
//

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

static struct map map = {
#if 0
    .data = (const u8 *) "################"
                         "#              #"
                         "#              #"
                         "#  ###    ###  #"
                         "#  #        #  #"
                         "#  #        #  #"
                         "#      ##      #"
                         "#  #        #  #"
                         "#  #        #  #"
                         "#      ##      #"
                         "#  #        #  #"
                         "#  #        #  #"
                         "#  ###    ###  #"
                         "#              #"
                         "#              #"
                         "################",
    .width = 16,
    .height = 16,
#elif 1
     .data = (const u8 *)
"##############################"
"#                            #"
"#                            #"
"#    ####################    #"
"#    #                  #    #"
"#    ####            ####    #"
"#                            #"
"#                            #"
"########              ########"
"#                            #"
"#  #                      #  #"
"#  #                      #  #"
"#                            #"
"#####  ################  #####"
"#                            #"
"#                            #"
"#                            #"
"#                            #"
"#  ##                    ##  #"
"#                            #"
"#                            #"
"#                            #"
"#            ####            #"
"#                            #"
"#            ####            #"
"#                            #"
"#                            #"
"#                            #"
"#                            #"
"##############################",
    .width = 30,
    .height = 30,
#elif 0
     .data = (const u8 *)
"####################################"
"#                           ##     #"
"#                           ##     #"
"#                          ###     #"
"#                                  #"
"#         #                ###     #"
"#         #                # #     #"
"#                          ###     #"
"##########                         #"
"#                          ###     #"
"#                          # #     #"
"#              #######     ###     #"
"#                                  #"
"#         #                ###     #"
"#                            #     #"
"#                           #      #"
"#                          #       #"
"#                          ###     #"
"#             ####                 #"
"#                     #            #"
"#                    #             #"
"#                   #   ######     #"
"#                                  #"
"#                #           #     #"
"#                #         ###     #"
"#                #                 #"
"#                #         ###     #"
"#                #         # #     #"
"#           #    #         ###     #"
"#                                  #"
"#        #########           #     #"
"#                          ###     #"
"#        #     #                   #"
"#        #                  #      #"
"#                                  #"
"####################################",
    .width = 36,
    .height = 36,
#endif
    .tile_size = 1.0f,
    .origin = {0, 0},
};

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

//
// Projectiles
//

Pack(struct nade_projectile {
    PlayerId player_id_from;
    v2 dir;
    v2 start_pos;
    v2 pos;
    f32 vel;
    v2 impact;
    f32 impact_distance;
    v2 impact_normal;
    f32 time_left;
});

struct explosion {
    PlayerId player_id_from;
    v2 pos;
    f32 radius;
    f32 time_left;
};

Pack(struct hitscan_projectile {
    PlayerId player_id_from;
    PlayerId player_id_to;
    v2 dir;
    v2 pos;
    v2 impact;
    f32 time_left;
});

struct damage_entry {
    PlayerId player_id;
    f32 damage;
};

//
// Hash map
//

enum {
    HASH_MAP_INVALID_HASH = 0,
};

static inline PlayerId player_id() {
    static PlayerId id = HASH_MAP_INVALID_HASH + 1;
    return id++;
}

#define HashMap(type, size)     \
    struct {                    \
        type data[size];        \
        size_t occupied[size];  \
        size_t num_items;       \
    }

#define HashMapForEach(map, type, iter) \
    for (type *iter = &map.data[0], *_top = &map.data[ARRLEN(map.data)]; iter != _top; ++iter)

#define HashMapExists(map, ptr) \
    (map.occupied[ArrayPtrToIndex(map.data, ptr)] != HASH_MAP_INVALID_HASH)

#define HashMapInsert(map, hash, output_value)                          \
    do {                                                                \
        assert(map.num_items < ARRLEN(map.data));                       \
                                                                        \
        /* Since num_items is less than the size of the data */         \
        /* array, we will eventually find a slot */                     \
        size_t index = hash % ARRLEN(map.data);                         \
        while (map.occupied[index] != HASH_MAP_INVALID_HASH) {          \
            index = (index + 1) % ARRLEN(map.data);                     \
        }                                                               \
                                                                        \
        /* Store our player */                                          \
        memset(&map.data[index], 0, sizeof(map.data[index]));           \
        map.data[index].id = hash;                                      \
        map.occupied[index] = hash;                                     \
        ++map.num_items;                                                \
                                                                        \
        output_value = &map.data[index];                                \
    } while (0)

#define HashMapLookup(map, hash, output_value)                          \
    do {                                                                \
        assert(hash != HASH_MAP_INVALID_HASH);                          \
        bool found = false;                                             \
        size_t index = HASH_MAP_INVALID_HASH;                           \
        for (size_t offset = 0; offset < ARRLEN(map.data); ++offset) {  \
            index = (hash + offset) % ARRLEN(map.data);                 \
            if (map.occupied[index] == hash) {                          \
                found = true;                                           \
                break;                                                  \
            }                                                           \
        }                                                               \
                                                                        \
        output_value =  &map.data[index];                               \
    } while (0)

#define HashMapRemove(map, hash)                                        \
    do {                                                                \
        void *_p = NULL;                                                \
        HashMapLookup(map, hash, _p);                                   \
        assert(_p != NULL);                                             \
                                                                        \
        size_t index = ArrayPtrToIndex(map.data, _p);                   \
        map.occupied[index] = HASH_MAP_INVALID_HASH;                    \
        --map.num_items;                                                \
    } while (0)
//
// Frame stuff
//

struct frame {
    u64 desired_delta;
    u64 delta;
    u64 simulation_tick;
    u64 network_tick;
    f32 dt;
};

//
// Debug
//

struct frame_debug_data {
    u32 incoming_data_total_start;
    u32 outgoing_data_total_start;
    u32 incoming_bandwidth;
    u32 outgoing_bandwidth;
    f32 fps;
    u64 total_frame_start;
    u64 total_delta;
};

//
// Game
//

#define PLAYER_HASH_MAP_SIZE 16
#define MAX_PROJECTILES 64
#define MAX_HITSCAN_PROJECTILES 64
#define MAX_SOUNDS_PER_FRAME 64
#define MAX_STEPS 128

struct spatial_sound {
    PlayerId player_id_from;
    enum sound sound;
    v2 pos;
};

struct step {
    PlayerId player_id_from;
    v2 pos;
    f32 time_left;
};

struct game {
    struct map map;

    HashMap(struct player, PLAYER_HASH_MAP_SIZE) player_map;

    List(struct hitscan_projectile, MAX_HITSCAN_PROJECTILES) hitscan_list;
    List(struct nade_projectile,    MAX_HITSCAN_PROJECTILES) nade_list;
    List(struct damage_entry,       MAX_CLIENTS)             damage_list;
    List(struct explosion,          MAX_HITSCAN_PROJECTILES) explosion_list;
    List(struct spatial_sound,      MAX_SOUNDS_PER_FRAME)    sound_list;

    // Would have been nicer to just use a render texture, but
    // wasn't able to get raylib to keep them alive across frames:(
    List(struct step, MAX_STEPS) step_list;

    // These are only used so that the server can easily batch new projectiles
    // and send to clients
    List(struct hitscan_projectile, MAX_HITSCAN_PROJECTILES) new_hitscan_list;
    List(struct nade_projectile,    MAX_HITSCAN_PROJECTILES) new_nade_list;
};

//
// Update functions
//

void update_player(struct game *game, struct player *p, struct input *input, const f32 dt);
void update_projectiles(struct game *game, const f32 dt);

//
// Collision detection
//

struct collision_result {
    PlayerId id0;
    PlayerId id1;
    bool colliding;
    v2 resolve;
};

struct aabb {
    v2 pos;
    f32 width;
    f32 height;
};

struct circle {
    v2 pos;
    f32 radius;
};

struct raycast_result {
    bool hit;
    v2 impact;
    v2 normal;
    f32 distance;
};

struct collision_result collide_circle_circle(struct circle c0, struct circle c1);
struct collision_result collide_aabb_circle(struct aabb aabb, struct circle circle);
struct raycast_result   collide_ray_circle(v2 pos, v2 dir, struct circle circle);
struct raycast_result   collide_ray_aabb(v2 pos, v2 dir, struct aabb aabb);
struct raycast_result   raycast_map(struct game *game, v2 pos, v2 dir);
struct raycast_result   raycast_players(struct game *game, v2 pos, v2 dir, struct player **hit_player);

void collect_and_resolve_static_collisions_for_player(struct game *game, struct player *p);
void collect_and_resolve_static_collisions(struct game *game);
void collect_dynamic_collisions(struct game *game, struct collision_result *results, u32 *num_results, u32 max_results);
void resolve_dynamic_collisions(struct game *game, struct collision_result *results, u32 num_results);
