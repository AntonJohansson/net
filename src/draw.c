#include "draw.h"
#include "update.h"
#include "random.h"
#include "color.h"

static inline const Vector2 get_scale() {
    const f32 width  = (f32) GetScreenWidth();
    const f32 height = (f32) GetScreenHeight();
    const f32 tile_size = 16.0f;
    const f32 scale_x = tile_size * ((width > height) ? (width/height) : 1.0f);
    const f32 scale_y = tile_size * ((width > height) ? 1.0f : (height/width));
    return (Vector2) {scale_x, scale_y};
}

static inline Vector2 world_to_screen(v2 v) {
    const f32 width  = (f32) GetScreenWidth();
    const f32 height = (f32) GetScreenHeight();
    Vector2 scale = get_scale();
    return (Vector2) {
        .x = width  * (v.x/scale.x + 0.5f),
        .y = height * (v.y/scale.y + 0.5f)
    };
}

Vector2 screen_to_world(Vector2 v) {
    const f32 width  = (f32) GetScreenWidth();
    const f32 height = (f32) GetScreenHeight();

    Vector2 scale = get_scale();
    v.x = scale.x*(v.x / width  - 0.5f);
    v.y = scale.y*(v.y / height - 0.5f);

    return v;
}

static inline f32 world_to_screen_length(f32 len) {
    const f32 width  = (f32) GetScreenWidth();
    const f32 height = (f32) GetScreenHeight();
    Vector2 scale = get_scale();
    const Vector2 v = world_to_screen((v2) {len - scale.x*0.5f, len - scale.y*0.5f});
    return fminf(v.x, v.y);
}

struct debug_v2 {
    v2 v;
    v2 pos;
    Color color;
    f32 scale;
};

#define MAX_DEBUG_V2S 512
static u32 num_debug_v2s = 0;
static struct debug_v2 debug_v2s[MAX_DEBUG_V2S] = {0};

void debug_v2(v2 pos, v2 v, f32 scale, Color color) {
    assert(num_debug_v2s < MAX_DEBUG_V2S);
    debug_v2s[num_debug_v2s].v = v;
    debug_v2s[num_debug_v2s].pos = pos;
    debug_v2s[num_debug_v2s].color = color;
    debug_v2s[num_debug_v2s].scale = scale;
    num_debug_v2s++;
}

void draw_all_debug_v2s() {
    for (u32 i = 0; i < num_debug_v2s; ++i) {
        struct debug_v2 *dv2 = &debug_v2s[i];
        draw_v2(dv2->pos, dv2->v, dv2->scale, dv2->color);
    }
    num_debug_v2s = 0;
}

void draw_centered_line(v2 start, v2 end, f32 thickness, Color color) {
    const v2 dir = v2normalize(v2sub(end, start));
    v2 ortho = { .x = -dir.y, .y = dir.x };

    start = v2add(start, v2scale(thickness/2.0f, dir));
    end = v2add(end, v2scale(thickness/2.0f, dir));

    DrawLineEx((Vector2) {start.x, start.y},
               (Vector2) {end.x, end.y},
               thickness,
               color);
}

void draw_v2(v2 pos, v2 v, f32 scale, Color color) {
    Vector2 start = world_to_screen(pos);
    Vector2 end = world_to_screen(v2add(pos, v2scale(scale, v)));
    draw_centered_line((v2) {start.x, start.y}, (v2) {end.x, end.y}, 2.0f, color);
    DrawCircle(start.x, start.y, 2.0f, color);
}

void draw_tile(f32 x, f32 y, f32 border_thickness, Color light, Color dark) {
    const f32 tile_size = 1.0f;
    const i32 screen_tile_size = ceilf(world_to_screen_length(tile_size));

    const i32 screen_border_thickness = world_to_screen_length(border_thickness*tile_size);
    const i32 screen_inner_tile_size = screen_tile_size - 2*screen_border_thickness;

    x = floorf(x);
    y = floorf(y);

    DrawRectangle(x, y, screen_tile_size, screen_tile_size, dark);
    DrawRectangle(x + screen_border_thickness,
                  y + screen_border_thickness,
                  screen_inner_tile_size,
                  screen_inner_tile_size,
                  light);
}

void draw_water(f32 t) {
    const f32 tile_size = 1.0f;
    const f32 width = (f32) GetScreenWidth();
    const f32 height = (f32) GetScreenHeight();

    const i32 screen_tile_size = world_to_screen_length(tile_size);

    const i32 num_tiles_x = (f32) width / screen_tile_size + 2;
    const i32 num_tiles_y = (f32) height / screen_tile_size + 2;

    struct random_series_pcg series = random_seed_pcg(0x1234, 0x5678);

    const Vector2 origin = world_to_screen((v2) {0.0f, 0.0f});

    for (i32 y = 0; y <= num_tiles_y; ++y) {
        for (i32 x = 0; x <= num_tiles_x; ++x) {
            const i32 rx = screen_tile_size * (x - num_tiles_x/2);
            const i32 ry = screen_tile_size * (y - num_tiles_y/2);

            const f32 t_scale = 1.5f;

            const f32 wave = 20.0f * cosf(2*M_PI*(0.7f*x - 0.3f*y)/30.0f     - 0.01f*t_scale*t) + 20.0f * sinf(2*M_PI*(0.7f*x - 0.3f*y)/30.0f - 0.01f*t_scale*t)
                           + 10.0f * cosf(2*M_PI*(0.9f*x - 0.3f*y)/23.0f     -  0.1f*t_scale*t) + 10.0f * sinf(2*M_PI*(0.9f*x - 0.3f*y)/23.0f -  0.1f*t_scale*t)
                           +  5.0f * cosf(2*M_PI*(0.3f*x - 0.7f*y)/11.0f     - 0.15f*t_scale*t) +  5.0f * sinf(2*M_PI*(0.7f*x - 0.3f*y)/11.0f - 0.15f*t_scale*t)
                           +  3.0f * cosf(2*M_PI*(0.3f*(x^y) - 0.7f*y)/11.0f -  0.3f*t_scale*t) +  3.0f * sinf(2*M_PI*(0.7f*x - 0.3f*y)/11.0f -  0.3f*t_scale*t);
            const f32 h = 200.0f + 0.30000f*wave;
            const f32 s = 0.35f  + 0.00100f*wave;
            const f32 l = 0.5f   + 0.00005f*wave*wave;
            const Color light = hsl_to_rgb(HSL(h, s, l));
            const Color dark  = hsl_to_rgb(HSL(h, s, 0.7f*l));

            const f32 thickness = fminf(0.0005f*wave*wave, 0.5);

            draw_tile(origin.x + rx, origin.y + ry, thickness, light, dark);
        }
    }
}

inline void draw_map(const struct map *map) {
    const f32 screen_tile_size = world_to_screen_length(map->tile_size);

    struct random_series_pcg series = random_seed_pcg(0x1234, 0x5678);

    const Vector2 origin = world_to_screen(map->origin);

    for (i32 y = 0; y < (i32) map->height; ++y) {
        for (i32 x = 0; x < (i32) map->width; ++x) {
            const f32 rx = screen_tile_size * (f32) x;
            const f32 ry = screen_tile_size * (f32) y;

            const u8 tile = map->data[y*map->width + x];

            const f32 tile_x = origin.x + rx;
            const f32 tile_y = origin.y + ry;

            switch (tile) {
            case '#': {
                const f32 lightness = 0.35f + 0.1f*random_next_bilateral(&series);

                const Color light = hsl_to_rgb(HSL(0.0f, 0.0f, lightness));
                const Color dark  = hsl_to_rgb(HSL(0.0f, 0.0f, 0.7f*lightness));

                const f32 thickness = 0.1f * random_next_unilateral(&series);

                draw_tile(tile_x, tile_y, thickness, light, dark);
            } break;
            case ' ': {
                const f32 hue = 120.0f + 50.0f*random_next_bilateral(&series);

                const Color light = hsl_to_rgb(HSL(hue, 0.25f, 0.5f));
                const Color dark  = hsl_to_rgb(HSL(hue, 0.25f, 0.4f));

                const f32 thickness = 0.05f * random_next_unilateral(&series);

                draw_tile(tile_x, tile_y, thickness, light, dark);
            } break;
            }
        }
    }
}

void draw_player(struct player *p) {
    const f32 line_len = 0.35f;
    const f32 line_thick = 0.25f;
    const f32 radius = 0.25f;
    const f32 dodge_radius = 0.7f * radius;

    Color light;
    Color dark;
    if (p->state == PLAYER_STATE_SLIDING || p->time_left_in_dodge_delay > 0.0f) {
        light = hsl_to_rgb(HSL(p->hue, 0.5f, 0.5f - 0.1f));
        dark  = hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f - 0.1f));
    } else {
        light = hsl_to_rgb(HSL(p->hue, 0.5f, 0.5f));
        dark  = hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f));
    }

    Vector2 pos = world_to_screen(p->pos);
    if (p->state == PLAYER_STATE_SLIDING) {
        DrawCircle(pos.x, pos.y, world_to_screen_length(dodge_radius), dark);
        DrawCircle(pos.x, pos.y, world_to_screen_length(0.7f*dodge_radius), light);
    } else {
        const f32 thickness = world_to_screen_length(line_thick);
        DrawLineEx(pos, world_to_screen(v2add(p->pos, v2scale(line_len, p->look))), thickness, DARKGRAY);
        DrawCircle(pos.x, pos.y, world_to_screen_length(radius), dark);
        DrawCircle(pos.x, pos.y, world_to_screen_length(0.7f*radius), light);
    }
}

void draw_game(struct game *game, const f32 t) {
    draw_water(t);
    draw_map(&game->map);
    for (u8 i = 0; i < MAX_CLIENTS; ++i) {
        struct player *player = &game->players[i];
        if (!player->occupied)
            continue;
        draw_player(player);
    }

    {
        const i32 x = GetMouseX();
        const i32 y = GetMouseY();
        const i32 size = 20;
        const i32 thickness = 2;
        DrawRectangle(x - size/2,      y - thickness/2, size, thickness, BLACK);
        DrawRectangle(x - thickness/2, y - size/2,      thickness, size, BLACK);
    }
}

void draw_graph(struct graph *g, v2 pos, v2 size, v2 margin) {
    Color bg = hsl_to_rgb(HSL(50.0f, 0.75f, 0.05f));
    bg.a = 0.75f * 255.0f;
    DrawRectangle(pos.x, pos.y, size.x, size.y, bg);
    for (u32 i = 0; i < g->size; ++i) {
        const f32 y = g->data[i];
        if (y < g->min)
            g->min = y;
        else if (y > g->max)
            g->max = y;
    }

    const f32 scale_x = (size.x - 2*margin.x) / (f32) (g->size-1);
    const f32 scale_y = (size.y - 2*margin.y) / (g->max - g->min);

    f32 last_x = 0.0f;
    f32 last_y = 0.0f;
    for (u32 i = 0; i < g->size; ++i) {
        const f32 x = pos.x + margin.x + scale_x * (f32) i;
        const f32 y = pos.y - margin.y + size.y - (scale_y*g->data[i] - scale_y*g->min);

        const i32 last_index = (g->top + g->size - 1) % g->size;
        const i32 dist = ((i32) g->size + last_index - (i32) i) % g->size;

        if (i > 0) {
            draw_centered_line((v2) {last_x, last_y},
                               (v2) {x, y},
                               2.0f,
                               hsl_to_rgb(HSL(50.0f, 0.75f, 0.5f - 0.4f*(f32)dist/(f32)g->size)));
        }

        DrawCircle(x, y, 4.0f, hsl_to_rgb(HSL(50.0f, 0.75f, 0.5f - 0.4f*(f32)dist/(f32)g->size)));

        last_x = x;
        last_y = y;
    }

    DrawLine(pos.x + margin.x + scale_x * (f32) g->top,
             pos.y,
             pos.x + +margin.y + scale_x * (f32) g->top,
             pos.y + size.y,
             GRAY);
}
