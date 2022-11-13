#pragma once

#include "common.h"
#include "v2.h"
#include <raylib.h>
#include <stdlib.h>

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

struct player;
struct map;
struct game;

Vector2 screen_to_world(Vector2 v);

void debug_v2(v2 pos, v2 v, f32 scale, Color color);
void draw_all_debug_v2s();

void draw_centered_line(v2 start, v2 end, f32 thickness, Color color);
void draw_v2(v2 pos, v2 v, f32 scale, Color color);
void draw_tile(f32 x, f32 y, f32 border_thickness, Color light, Color dark);
void draw_water(f32 t);
void draw_map(const struct map *map);
void draw_player(struct player *p);
void draw_game(struct game *game, const f32 t);
void draw_graph(struct graph *g, v2 pos, v2 size, v2 margin);
