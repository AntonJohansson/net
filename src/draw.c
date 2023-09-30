#include "draw.h"
#include "random.h"
#include "color.h"
#include "game.h"

#include <rlgl.h>

RenderTexture lightmap;
Shader shadow;
Shader light;
Shader final;

int light_resolution = 512;
int shadow_resolution = 1024;

int light_pos_loc;
int resolution_loc;
int shadow_resolution_light_loc;
int shadow_resolution_shadow_loc;
int light_resolution_loc;
int light_mode_loc;
int cone_angle_loc;
int cone_width_loc;
int cone_length_loc;

static inline const Vector2 get_scale() {
    const f32 width  = (f32) GetRenderWidth();
    const f32 height = (f32) GetRenderHeight();
    const f32 tile_size = 30.0f;
    const f32 scale_x = tile_size * ((width > height) ? (width/height) : 1.0f);
    const f32 scale_y = tile_size * ((width > height) ? 1.0f : (height/width));
    return (Vector2) {scale_x, scale_y};
}

Vector2 world_to_screen(struct camera c, v2 v) {
    const f32 width  = (f32) GetRenderWidth();
    const f32 height = (f32) GetRenderHeight();
    Vector2 scale = get_scale();
    return (Vector2) {
        .x = width  * ((v.x - c.target.x)/scale.x) + c.offset.x,
        .y = height * ((v.y - c.target.y)/scale.y) + c.offset.y,
    };
}

v2 screen_to_world(struct camera c, Vector2 v) {
    const f32 width  = (f32) GetRenderWidth();
    const f32 height = (f32) GetRenderHeight();

    v2 res;
    Vector2 scale = get_scale();
    res.x = scale.x*((v.x - c.offset.x) / width) + c.target.x;
    res.y = scale.y*((v.y - c.offset.y) / height) + c.target.y;

    return res;
}

static inline f32 world_to_screen_length(struct camera c, f32 len) {
    const f32 width  = (f32) GetRenderWidth();
    const f32 height = (f32) GetRenderHeight();
    Vector2 scale = get_scale();
    const Vector2 v = world_to_screen((struct camera){0}, (v2) {len, len});
    return fminf(v.x, v.y);
}

static inline void set_light_resolution() {
    light_resolution = (int) world_to_screen_length((struct camera){0}, 32.0f);
}

void draw_init() {
    SetTraceLogLevel(LOG_ERROR);
    InitWindow(800, 600, "floating");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    HideCursor();

    set_light_resolution();

    shadow = LoadShader("res/shadow.vert", "res/shadow.frag");
    light = LoadShader("res/light.vert", "res/light.frag");
    final = LoadShader("res/final.vert", "res/final.frag");

    lightmap = LoadRenderTexture(GetRenderWidth(), GetRenderHeight());

    light_pos_loc                = GetShaderLocation(light, "light_pos");
    resolution_loc               = GetShaderLocation(light, "resolution");
    shadow_resolution_light_loc  = GetShaderLocation(light, "shadow_resolution");
    shadow_resolution_shadow_loc = GetShaderLocation(shadow, "shadow_resolution");
    light_resolution_loc         = GetShaderLocation(light, "light_resolution");
    light_mode_loc               = GetShaderLocation(light, "light_mode");
    cone_angle_loc               = GetShaderLocation(light, "cone_angle");
    cone_width_loc               = GetShaderLocation(light, "cone_width");
    cone_length_loc              = GetShaderLocation(light, "cone_length");
}

void draw_deinit() {
    UnloadShader(final);
    UnloadShader(light);
    UnloadShader(shadow);
    UnloadRenderTexture(lightmap);

    CloseWindow();
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

void draw_v2(struct camera c, v2 pos, v2 v, f32 scale, Color color) {
    Vector2 start = world_to_screen(c, pos);
    Vector2 end = world_to_screen(c, v2add(pos, v2scale(scale, v)));
    draw_centered_line((v2) {start.x, start.y}, (v2) {end.x, end.y}, 2.0f, color);
    DrawCircle(start.x, start.y, 2.0f, color);
}

void draw_all_debug_v2s(struct camera c) {
    for (u32 i = 0; i < num_debug_v2s; ++i) {
        struct debug_v2 *dv2 = &debug_v2s[i];
        draw_v2(c, dv2->pos, dv2->v, dv2->scale, dv2->color);
    }
    num_debug_v2s = 0;
}

void draw_tile(struct camera c, f32 x, f32 y, f32 border_thickness, Color light, Color dark) {
    const f32 tile_size = 1.0f;
    const i32 screen_tile_size = ceilf(world_to_screen_length(c, tile_size));

    const i32 screen_border_thickness = world_to_screen_length(c, border_thickness*tile_size);
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

void draw_occluders(struct camera c, const struct map *map, RenderTexture occlusionmap) {
    const f32 screen_tile_size = world_to_screen_length(c, map->tile_size);
    const Vector2 origin = world_to_screen(c, map->origin);

    BeginTextureMode(occlusionmap);
    ClearBackground(BLANK);
    for (i32 y = 0; y < (i32) map->height; ++y) {
        for (i32 x = 0; x < (i32) map->width; ++x) {
            const f32 rx = screen_tile_size * (f32) x;
            const f32 ry = screen_tile_size * (f32) y;

            const u8 tile = map->data[y*map->width + x];

            const f32 tile_x = origin.x + rx;
            const f32 tile_y = origin.y + ry;

            switch (tile) {
            case '#': {
                draw_tile(c, tile_x, tile_y, 0.0f, WHITE, WHITE);
            } break;
            }
        }
    }
    EndTextureMode();
}

void draw_map(struct camera c, const struct map *map) {
    const f32 screen_tile_size = world_to_screen_length(c, map->tile_size);

    struct random_series_pcg series = random_seed_pcg(0x1234, 0x5678);

    const Vector2 origin = world_to_screen(c, map->origin);

    for (i32 y = 0; y < (i32) map->height; ++y) {
        for (i32 x = 0; x < (i32) map->width; ++x) {
            const f32 rx = screen_tile_size * (f32) x;
            const f32 ry = screen_tile_size * (f32) y;

            const u8 tile = map->data[y*map->width + x];

            const f32 tile_x = origin.x + rx;
            const f32 tile_y = origin.y + ry;

            switch (tile) {
            case '#': {
                const f32 lightness = 0.18f + 0.04f*random_next_bilateral(&series);

                const Color light = hsl_to_rgb(HSL(0.0f, 0.0f,      lightness));
                const Color dark  = hsl_to_rgb(HSL(0.0f, 0.0f, 0.7f*lightness));

                const f32 thickness = 0.1f * random_next_unilateral(&series);

                draw_tile(c, tile_x, tile_y, thickness, light, dark);
            } break;
            case ' ': {
                const f32 hue = 120.0f + 50.0f*random_next_bilateral(&series);

                const Color light = hsl_to_rgb(HSL(hue, 0.45f, 0.2f));
                const Color dark  = hsl_to_rgb(HSL(hue, 0.45f, 0.1f));

                const f32 thickness = 0.05f * random_next_unilateral(&series);

                draw_tile(c, tile_x, tile_y, thickness, light, dark);
            } break;
            }
        }
    }
}

void draw_player(struct camera c, struct player *p) {
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

    Vector2 pos = world_to_screen(c, p->pos);

    if (p->state == PLAYER_STATE_SLIDING) {
        DrawCircle(pos.x, pos.y, world_to_screen_length(c, dodge_radius), dark);
        DrawCircle(pos.x, pos.y, world_to_screen_length(c, 0.7f*dodge_radius), light);
    } else {
        v2 ortho_dir = {-p->look.y, p->look.x};

        for (u32 i = 0; i < 2; ++i) {

            const f32 cooldown = p->time_left_in_weapon_cooldown[i];

            switch (p->weapons[i]) {

            case PLAYER_WEAPON_SNIPER: {
                if (i == p->current_weapon) {
                    v2 sniper_end = v2add(p->pos, v2scale(0.8f, p->look));
                    DrawLineEx(world_to_screen(c, p->pos),
                               world_to_screen(c, sniper_end),
                               world_to_screen_length(c, 0.1f),
                               hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f*(weapon_sniper_cooldown-cooldown))));
                } else {
                    v2 sniper_start = v2add(v2add(p->pos, v2scale(0.40f, ortho_dir)), v2scale(-0.25f, p->look));
                    v2 sniper_end = v2add(sniper_start, v2scale(-0.8f, ortho_dir));
                    DrawLineEx(world_to_screen(c, sniper_start),
                               world_to_screen(c, sniper_end),
                               world_to_screen_length(c, 0.1f),
                               hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f*(weapon_sniper_cooldown-cooldown))));
                }
            } break;

            case PLAYER_WEAPON_NADE: {
                if (i == p->current_weapon) {
                    v2 nade_dist_end = v2add(p->pos, v2scale(p->nade_distance, p->look));
                    DrawLineEx(world_to_screen(c, p->pos),
                               world_to_screen(c, nade_dist_end),
                               world_to_screen_length(c, 0.1f),
                               hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f*(weapon_nade_cooldown-cooldown))));

                    const f32 nade_radius = 0.125f * (weapon_nade_cooldown - cooldown);
                    Vector2 nade_pos = world_to_screen(c, v2add(p->pos, v2scale(0.25f+0.125f, p->look)));
                    DrawCircle(nade_pos.x, nade_pos.y, world_to_screen_length(c, nade_radius), light);
                    DrawCircle(nade_pos.x, nade_pos.y, world_to_screen_length(c, 0.7f*nade_radius), dark);
                } else {
                    const f32 nade_radius = 0.125f * (weapon_nade_cooldown - cooldown);
                    Vector2 nade_pos = world_to_screen(c, v2add(p->pos, v2scale(-0.25f-0.125f, p->look)));
                    DrawCircle(nade_pos.x, nade_pos.y, world_to_screen_length(c, nade_radius), light);
                    DrawCircle(nade_pos.x, nade_pos.y, world_to_screen_length(c, 0.7f*nade_radius), dark);
                }
            } break;
            }
        }

        DrawCircle(pos.x, pos.y, world_to_screen_length(c, radius), dark);
        DrawCircle(pos.x, pos.y, world_to_screen_length(c, 0.7f*radius), light);
    }
}

enum light_mode {
    LIGHT_MODE_POINT = 0,
    LIGHT_MODE_CONE,
};

struct light {
    bool active;
    // NOTE(anjo): The way we deal with constructiono shadow maps,
    //             as we do it single threadedly, we don't need
    //             a separate occlusionmap for each light.
    //
    //             @OPTIMIZATION
    RenderTexture occlusionmap;
    RenderTexture shadowmap;
    enum light_mode mode;
    v2 pos;
    Color color;
    // These last entries are only used by cone lights
    f32 cone_angle;
    f32 cone_width;
    f32 cone_length;
};

#define MAX_LIGHTS 16
static struct light lights[MAX_LIGHTS] = {0};
static u32 num_lights = 0;

void draw_dynamic_light(struct game *game, enum light_mode mode, v2 pos, f32 cone_angle, f32 cone_width, f32 cone_length, Color color) {
    assert(num_lights < MAX_LIGHTS);

    struct light *l = &lights[num_lights++];
    if (!l->active) {
        l->occlusionmap = LoadRenderTexture(light_resolution, light_resolution);
        l->shadowmap = LoadRenderTexture(shadow_resolution, 1);
        SetTextureFilter(l->occlusionmap.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(l->occlusionmap.texture, TEXTURE_WRAP_REPEAT);
        l->active = true;
    }

    l->mode = mode;
    l->pos = pos;
    l->color = color;
    l->cone_angle = cone_angle;
    l->cone_width= cone_width;
    l->cone_length = cone_length;

    {
        struct camera c = {
            .target = l->pos,
            .offset = {light_resolution/2, light_resolution/2},
        };
        draw_occluders(c, &game->map, l->occlusionmap);
    }

    BeginShaderMode(shadow);
    SetShaderValue(shadow, shadow_resolution_shadow_loc, &shadow_resolution, SHADER_UNIFORM_INT);
    BeginTextureMode(l->shadowmap);
    ClearBackground(BLANK);
    DrawTextureRec(l->occlusionmap.texture,
                   (Rectangle){0,0,l->occlusionmap.texture.width,l->occlusionmap.texture.height},
                   (Vector2){0,0}, WHITE);
    EndTextureMode();
    EndShaderMode();
}

void draw_game(struct camera c, struct game *game, PlayerId main_player_id, const f32 dt, const f32 t) {
    if (IsWindowResized()) {
        set_light_resolution();

        UnloadRenderTexture(lightmap);
        lightmap = LoadRenderTexture(GetRenderWidth(), GetRenderHeight());

        // Invalidate all lights
        for (u32 i = 0; i < num_lights; ++i) {
            struct light *l = &lights[i];
            UnloadRenderTexture(l->occlusionmap);
            UnloadRenderTexture(l->shadowmap);
            l->active = false;
        }
    }

    num_lights = 0;

    struct player *main_player = NULL;
    HashMapLookup(game->player_map, main_player_id, main_player);

    if (main_player->health > 0.0f) {
        f32 cone_angle = v2angle(main_player->look);

        f32 zoom = M_PI/2.0f;
        if (main_player->weapons[main_player->current_weapon] == PLAYER_WEAPON_SNIPER && main_player->sniper_zoom > 0.0f) {
            zoom = M_PI/(2.0f + 8.0f*main_player->sniper_zoom);
        }

        f32 length = 0.5f + main_player->sniper_zoom/2.0f;

        draw_dynamic_light(game, LIGHT_MODE_CONE, main_player->pos, cone_angle, zoom, length, (Color){200,200,200,200});
    }

    // Draw nade blinky lights
    ForEachList(game->nade_list, struct nade_projectile, nade) {
        // Don't draw lights for other players nades
        if (nade->player_id_from != main_player_id)
            continue;

        struct player *p;
        HashMapLookup(game->player_map, nade->player_id_from, p);

        const Color light = hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f));

        f32 s = sinf((M_PI/2.0f) * (f32)(nade->time_left/ dt)/64);
        f32 radius = 0.25f * s*s*s*s;
        draw_dynamic_light(game, LIGHT_MODE_POINT, nade->pos, 0, 0, radius, light);
    }

    //for (u8 i = 0; i < MAX_CLIENTS; ++i) {
    //    struct player *player = &game->players[i];
    //    if (!player->occupied || player->health == 0.0f)
    //        continue;

    //    f32 cone_angle = M_PI + atan2f(player->look.y, player->look.x);
    //    draw_dynamic_light(game, LIGHT_MODE_CONE, player->pos, cone_angle, M_PI/2.0f);
    //}

    Vector2 resolution = {GetRenderWidth(), GetRenderHeight()};

    BeginTextureMode(lightmap);
    ClearBackground(BLANK);
    for (u32 i = 0; i < num_lights; ++i) {
        struct light *l = &lights[i];
        Vector2 light_screen_pos = world_to_screen(c, l->pos);

        BeginShaderMode(light);
        BeginBlendMode(BLEND_ADDITIVE);


        SetShaderValue(light, shadow_resolution_light_loc, &shadow_resolution, SHADER_UNIFORM_INT);
        SetShaderValue(light, light_resolution_loc, &light_resolution, SHADER_UNIFORM_INT);
        SetShaderValue(light, light_pos_loc, &light_screen_pos, SHADER_UNIFORM_VEC2);
        SetShaderValue(light, resolution_loc, &resolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(light, light_mode_loc, &l->mode, SHADER_UNIFORM_INT);
        SetShaderValue(light, cone_angle_loc, &l->cone_angle, SHADER_UNIFORM_FLOAT);
        SetShaderValue(light, cone_width_loc, &l->cone_width, SHADER_UNIFORM_FLOAT);
        SetShaderValue(light, cone_length_loc, &l->cone_length, SHADER_UNIFORM_FLOAT);

        DrawTextureRec(l->shadowmap.texture,
                       (Rectangle){0,0,light_resolution,light_resolution},
                       (Vector2){light_screen_pos.x - light_resolution/2,
                                 light_screen_pos.y - light_resolution/2},
                       l->color);

        EndBlendMode();
        EndShaderMode();
    }
    EndTextureMode();

    draw_map(c, &game->map);

    DrawRectangle(0,0,resolution.x,resolution.y,(Color){10,10,10,150});
    BeginBlendMode(BLEND_ADDITIVE);
    DrawTextureRec(lightmap.texture, (Rectangle){0,0,lightmap.texture.width,-lightmap.texture.height}, (Vector2){0,0}, WHITE);
    EndBlendMode();

    BeginShaderMode(final);
    SetShaderValueTexture(final, GetShaderLocation(final, "lightmap"), lightmap.texture);
    SetShaderValue(final, GetShaderLocation(final, "resolution"), &resolution, SHADER_UNIFORM_VEC2);

    ForEachList(game->step_list, struct step, s) {
        struct player *p;
        HashMapLookup(game->player_map, s->player_id_from, p);

        const Color dark   = Fade(hsl_to_rgb(HSL(p->hue, 0.5f, 0.2f)), s->time_left/2.0f);
        const Color darker = Fade(hsl_to_rgb(HSL(p->hue, 0.5f, 0.1f)), s->time_left/2.0f);
        DrawCircleV(world_to_screen(c, s->pos), world_to_screen_length(c, 0.2f), darker);
        DrawCircleV(world_to_screen(c, s->pos), world_to_screen_length(c, 0.7f*0.2f), dark);
    }

    HashMapForEach(game->player_map, struct player, p) {
        if (!HashMapExists(game->player_map, p))
            continue;
        if (p->id == main_player_id || p->health == 0.0f)
            continue;
        draw_player(c, p);
    }

    ForEachList(game->nade_list, struct nade_projectile, nade) {
        struct player *p;
        HashMapLookup(game->player_map, nade->player_id_from, p);

        const Color light = hsl_to_rgb(HSL(p->hue, 0.5f, 0.5f));
        const Color dark  = hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f));

        Vector2 nade_pos = world_to_screen(c, nade->pos);
        DrawCircle(nade_pos.x, nade_pos.y, world_to_screen_length(c, 0.125f), light);
        DrawCircle(nade_pos.x, nade_pos.y, world_to_screen_length(c, 0.7f*0.125f), dark);
    }

    ForEachList(game->explosion_list, struct explosion, e) {
        struct player *p;
        HashMapLookup(game->player_map, e->player_id_from, p);

        const Color dark = Fade(hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f)), e->time_left);

        f32 radius = world_to_screen_length(c, e->radius);
        Vector2 explosion_pos = world_to_screen(c, e->pos);
        DrawCircle(explosion_pos.x, explosion_pos.y, radius, dark);
    }

    ForEachList(game->hitscan_list, struct hitscan_projectile, hitscan) {
        struct player *p;
        HashMapLookup(game->player_map, hitscan->player_id_from, p);

        const Color dark = Fade(hsl_to_rgb(HSL(p->hue, 0.5f, 0.3f)), hitscan->time_left);

        Vector2 start = world_to_screen(c, hitscan->pos);
        Vector2 end = world_to_screen(c, hitscan->impact);
        f32 thickness = world_to_screen_length(c, 0.1f);
        DrawLineEx(start, end, thickness, dark);
        f32 radius = world_to_screen_length(c, 0.1f);
        DrawCircle(end.x, end.y, radius, dark);
    }

    EndShaderMode();

    if (main_player->health > 0.0f)
        draw_player(c, main_player);

    // Draw crosshair
    {
        const i32 x = GetMouseX();
        const i32 y = GetMouseY();
        const i32 size = 20;
        const i32 thickness = 2;
        DrawRectangle(x - size/2,      y - thickness/2, size, thickness, WHITE);
        DrawRectangle(x - thickness/2, y - size/2,      thickness, size, WHITE);
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
