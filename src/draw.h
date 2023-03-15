#pragma once

#include "common.h"
#include "v2.h"
#include "game.h"

#include <raylib.h>
#include <string.h>

struct graph {
    f32 *data;
    f32 max;
    f32 min;
    u32 size;
    u32 top;
};

static struct graph graph_new(u32 size) {
    struct graph g = {
        .data = malloc(size * sizeof(f32)),
        .max = 1.0f,
        .min = 0.0f,
        .size = size,
        .top = 0,
    };
    memset(g.data, 0, size * sizeof(f32));
    return g;
}

static void graph_free(struct graph *g) {
    free(g->data);
    g->size = 0;
    g->data = NULL;
}

static void graph_append(struct graph *g, f32 y) {
    g->data[g->top] = y;
    g->top = (g->top + 1) % g->size;
}

struct camera {
    v2 offset;
    v2 target;
};

struct player;
struct map;
struct game;

void draw_init();
void draw_deinit();

v2 screen_to_world(struct camera c, Vector2 v);
Vector2 world_to_screen(struct camera c, v2 v);

void debug_v2(v2 pos, v2 v, f32 scale, Color color);
void draw_all_debug_v2s(struct camera c);

void draw_game(struct camera c, struct game *game, PlayerId main_player_id, const f32 dt, const f32 t);
void draw_graph(struct graph *g, v2 pos, v2 size, v2 margin);
