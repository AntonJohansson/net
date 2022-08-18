#define ENET_IMPLEMENTATION
#include "enet.h"

#include "packet.h"
#include "common.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include "random.h"
#if !defined(STRESS)
#include <raylib.h>
#include "color.h"
#endif

#define WIDTH 800
#define HEIGHT 600

#define PACKET_LOG_SIZE 2048
#define OUTPUT_BUFFER_SIZE 2048
#define INPUT_BUFFER_LENGTH 512
#define UPDATE_LOG_BUFFER_SIZE 512

const u64 initial_server_net_tick_offset = 5;

bool running = true;

void inthandler(int sig) {
    (void) sig;
    running = false;
}

bool connected = false;

struct update_log_buffer {
    struct server_packet_peer_auth data[UPDATE_LOG_BUFFER_SIZE];
    u64 bottom;
    u64 used;
};

struct peer {
    bool connected;
    struct player player;
    struct update_log_buffer update_log;
};

static inline void peer_disconnect(struct peer *p) {
    memset(p, 0, sizeof(struct peer));
}

static inline void new_packet(struct byte_buffer *output_buffer) {
    struct client_batch_header *batch = (void *) output_buffer->base;
    assert(batch->num_packets < UINT16_MAX);
    ++batch->num_packets;
}

#if !defined(STRESS)
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

static inline Vector2 screen_to_world(Vector2 v) {
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

void client_handle_input(struct player *p, struct input *input) {
    if (IsKeyDown(KEY_W))
        input->active[INPUT_MOVE_UP] = true;
    if (IsKeyDown(KEY_A))
        input->active[INPUT_MOVE_LEFT] = true;
    if (IsKeyDown(KEY_S))
        input->active[INPUT_MOVE_DOWN] = true;
    if (IsKeyDown(KEY_D))
        input->active[INPUT_MOVE_RIGHT] = true;
    if (IsKeyDown(KEY_LEFT_SHIFT))
        input->active[INPUT_MOVE_DODGE] = true;
    if (IsKeyDown(KEY_Q))
        input->active[INPUT_QUIT] = true;

    Vector2 v = screen_to_world((Vector2) {GetMouseX(), GetMouseY()});
    input->x = v.x - p->pos.x;
    input->y = v.y - p->pos.y;
}

static void draw_tile(f32 x, f32 y, f32 border_thickness, Color light, Color dark) {
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

static void draw_water(f32 t) {
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

            const f32 wave = 20.0f * cosf(2*M_PI*(0.7f*x - 0.3f*y)/30.0f     - 0.01f*t) + 20.0f * sinf(2*M_PI*(0.7f*x - 0.3f*y)/30.0f - 0.01f*t)
                           + 10.0f * cosf(2*M_PI*(0.9f*x - 0.3f*y)/23.0f     - 0.1f*t) + 10.0f * sinf(2*M_PI*(0.9f*x - 0.3f*y)/23.0f - 0.1f*t)
                           +  5.0f * cosf(2*M_PI*(0.3f*x - 0.7f*y)/11.0f     - 0.15f*t) +  5.0f * sinf(2*M_PI*(0.7f*x - 0.3f*y)/11.0f - 0.15f*t)
                           +  3.0f * cosf(2*M_PI*(0.3f*(x^y) - 0.7f*y)/11.0f - 0.3f*t) +  3.0f * sinf(2*M_PI*(0.7f*x - 0.3f*y)/11.0f - 0.3f*t);
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

static inline void draw_map(const struct map *map) {
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

static inline void draw_player(struct player *p, f32 base_hue) {
    const f32 line_len = 0.35f;
    const f32 line_thick = 0.25f;
    const f32 radius = 0.25f;
    const f32 dodge_radius = 0.7f * radius;

    Color light = hsl_to_rgb(HSL(base_hue, 0.5f, 0.5f));
    Color dark  = hsl_to_rgb(HSL(base_hue, 0.5f, 0.3f));

    Vector2 pos = world_to_screen(p->pos);
    if (p->time_left_in_dodge > 0.0) {
        DrawCircle(pos.x, pos.y, world_to_screen_length(dodge_radius), dark);
        DrawCircle(pos.x, pos.y, world_to_screen_length(0.7f*dodge_radius), light);
    } else {
        const f32 thickness = world_to_screen_length(line_thick);
        DrawLineEx(pos, world_to_screen(v2add(p->pos, v2scale(line_len, p->look))), thickness, DARKGRAY);
        DrawCircle(pos.x, pos.y, world_to_screen_length(radius), dark);
        DrawCircle(pos.x, pos.y, world_to_screen_length(0.7f*radius), light);
    }
}
#endif

int main(int argc, char **argv) {
    signal(SIGINT, inthandler);

#if defined(STRESS)
    assert(argc > 1);
    const u32 stress_scale = 1;
    const u32 stress_n = (u32)atoi(argv[1]);
    u32 stress_init_r_frames = (stress_scale * stress_n) / 2;
    u32 stress_init_u_frames = (stress_scale * stress_n) / 2;
    u32 stress_l_frames = (stress_scale * stress_n);
    u32 stress_d_frames = (stress_scale * stress_n);
    u32 stress_r_frames = (stress_scale * stress_n);
    u32 stress_u_frames = (stress_scale * stress_n);
#endif

    struct byte_buffer output_buffer = {
        .base = malloc(OUTPUT_BUFFER_SIZE),
        .size = OUTPUT_BUFFER_SIZE,
    };
    output_buffer.top = output_buffer.base;

    {
        struct client_batch_header batch = {0};
        APPEND(&output_buffer, &batch);
    }

    if (enet_initialize() != 0) {
        fprintf(stderr, "An error occurred while initializing ENet.\n");
        return EXIT_FAILURE;
    }

    ENetHost *client = {0};
    client = enet_host_create(NULL, 1, 1, 0, 0);
    if (client == NULL) {
        fprintf(stderr, "An error occurred while trying to create an ENet client host.\n");
        exit(EXIT_FAILURE);
    }

    ENetAddress address = {0};
    ENetEvent event = {0};
    ENetPeer *peer = {0};
    enet_address_set_host(&address, "localhost");
    address.port = 9053;

    peer = enet_host_connect(client, &address, 2, 0);
    if (peer == NULL) {
        fprintf(stderr, "No available peers for initiating an ENet connection.\n");
        exit(EXIT_FAILURE);
    }
    /* Wait up to 5 seconds for the connection attempt to succeed. */
    if (enet_host_service(client, &event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        puts("Connection to some.server.net:1234 succeeded.");
    } else {
        /* Either the 5 seconds are up or a disconnect event was */
        /* received. Reset the peer in the event the 5 seconds   */
        /* had run out without any significant event.            */
        enet_peer_reset(peer);
        puts("Connection to some.server.net:1234 failed.");
    }

#if !defined(STRESS)
    InitWindow(WIDTH, HEIGHT, "client");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    DisableCursor();
#endif

    struct timespec frame_start = {0},
                    frame_end   = {0},
                    frame_delta = {0},
                    frame_diff  = {0};

    struct timespec frame_desired = {
        .tv_sec = 0,
        .tv_nsec = NANOSECS_PER_SEC / FPS,
    };

    const f32 dt = time_nanoseconds(&frame_desired) / (f32) NANOSECS_PER_SEC;
    f32 t = 0.0f;
    f32 fps = 0.0f;

    u8 input_count = 0;
    struct input input_buffer[INPUT_BUFFER_LENGTH] = {0};

    struct peer peers[MAX_CLIENTS] = {0};
    u8 num_peers = 0;
    u8 main_peer_index;

    u64 sim_tick = 0;
    u64 net_tick = 0;
    i8 adjustment = 0;
    u8 adjustment_iteration = 0;
    i32 total_adjustment = 0;
    u64 pre_adjustment_sim_tick = 0;
    u64 init_sim_tick = 0;

    while (running) {
#if defined(STRESS)
        if ((sim_tick - init_sim_tick) / FPS >= 60) {
            running = false;
        }
#endif
        // Begin frame
        time_current(&frame_start);

        if (sim_tick % NET_PER_SIM_TICKS == 0) {
            // Adjustment when ahead of server
            if (adjustment < 0) {
                for (u32 i = 0; i < NET_PER_SIM_TICKS; ++i)
                    nanosleep(&frame_desired, NULL);
                // NOTE(anjo): I don't remember why this is here.
                //             But it doesn't work without it.
                if (++adjustment == 0) {
                    net_tick++;
                    sim_tick += NET_PER_SIM_TICKS;
                }
                continue;
            }

            // Fetch network data
            while (enet_host_service(client, &event, 0) > 0) {
                switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    // Packet batch header
                    char *p = event.packet->data;
                    struct server_batch_header *batch = (struct server_batch_header *) p;
                    p += sizeof(struct server_batch_header);

                    if (batch->adjustment != 0 && adjustment_iteration == batch->adjustment_iteration) {
                        adjustment = batch->adjustment;
                        total_adjustment += adjustment;
                        pre_adjustment_sim_tick = sim_tick;
                        ++adjustment_iteration;
                    }

                    for (u16 packet = 0; packet < batch->num_packets; ++packet) {
                        struct server_header *header = (struct server_header *) p;
                        p += sizeof(struct server_header);

                        // Packet payload
                        switch (header->type) {
                        case SERVER_PACKET_GREETING: {
                            struct server_packet_greeting *greeting = (struct server_packet_greeting *) p;
                            p += sizeof(struct server_packet_greeting);

                            net_tick = greeting->initial_net_tick + initial_server_net_tick_offset;
                            sim_tick = net_tick * NET_PER_SIM_TICKS;
                            init_sim_tick = net_tick * NET_PER_SIM_TICKS;
                            main_peer_index = greeting->peer_index;
                            assert(!peers[main_peer_index].connected);
                            peers[main_peer_index].connected = true;
                            peers[main_peer_index].player.pos = greeting->initial_pos;
                            ++num_peers;
                            connected = true;
                        } break;
                        case SERVER_PACKET_PEER_GREETING: {
                            struct server_packet_peer_greeting *greeting = (struct server_packet_peer_greeting *) p;
                            p += sizeof(struct server_packet_peer_greeting);

                            u8 peer_index = greeting->peer_index;
                            peers[peer_index].player.pos = greeting->initial_pos;
                            ++num_peers;
                        } break;
                        case SERVER_PACKET_AUTH: {
                            struct server_packet_auth *auth = (struct server_packet_auth *) p;
                            p += sizeof(struct server_packet_auth);

                            assert(auth->sim_tick <= sim_tick);
                            u64 diff = sim_tick - auth->sim_tick - 1;
                            assert(diff < INPUT_BUFFER_LENGTH);

                            struct player *player = &peers[main_peer_index].player;


                            // Gets the input for the sim_tick after the sim_tick we recieved auth data for
                            struct player old_player = auth->player;
                            u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                            for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                                struct input *old_input = &input_buffer[old_index];
                                move(&map, &old_player, old_input, dt);
                            }

                            if (!v2equal(player->pos, old_player.pos)) {
                                {
                                    printf("  Replaying %d inputs from %d+1 to %d-1\n", diff, auth->sim_tick, sim_tick);
                                    printf("    Starting from {%f, %f}\n", auth->player.pos.x, auth->player.pos.y);
                                    printf("    Should match {%f, %f}\n", player->pos.x, player->pos.y);

                                    struct player tmp_player = auth->player;
                                    u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                                    for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                                        struct input *old_input = &input_buffer[old_index];
                                        move(&map, &tmp_player, old_input, dt);
                                        printf("    -> {%f, %f}\n", tmp_player.pos.x, tmp_player.pos.y);
                                    }
                                }

                                printf("  Server disagreed! {%f, %f} vs {%f, %f}\n", player->pos.x, player->pos.y, old_player.pos.x, old_player.pos.y);
                                *player = auth->player;
                            }
                        } break;
                        case SERVER_PACKET_PEER_AUTH: {
                            struct server_packet_peer_auth *peer_auth = (struct server_packet_peer_auth *) p;
                            p += sizeof(struct server_packet_peer_auth);

                            const u8 peer_index = peer_auth->peer_index;
                            assert(peer_index >= 0 && peer_index < num_peers);
                            struct peer *peer = &peers[peer_index];
                            CIRCULAR_BUFFER_APPEND(&peer->update_log, *peer_auth);
                        } break;
                        case SERVER_PACKET_PEER_DISCONNECTED: {
                            struct server_packet_peer_disconnected *disc = (struct server_packet_peer_disconnected *) p;
                            p += sizeof(struct server_packet_peer_disconnected);

                            printf("%d disconnected!\n", disc->peer_index);
                            peer_disconnect(&peers[disc->peer_index]);
                            --num_peers;
                        } break;
                        default:
                            printf("Received unknown packet type %d\n", header->type);
                        }
                    }
                } break;

                case ENET_EVENT_TYPE_DISCONNECT:
                    printf("Server disconnected\n");
                    break;

                case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                    printf("Server timeout\n");
                    break;

                case ENET_EVENT_TYPE_NONE:
                    break;
                }
                enet_packet_destroy(event.packet);
            }
        }

        u64 active_tick = sim_tick + 2*total_adjustment;
        for (u8 i = 0; i < num_peers; ++i) {
            if (i == main_peer_index)
                continue;
            struct peer *peer = &peers[i];
            if (peer->update_log.used > 0) {
                struct server_packet_peer_auth *entry = &peer->update_log.data[peer->update_log.bottom];

                if (active_tick < entry->sim_tick)
                    continue;

                peer->player = entry->player;

                CIRCULAR_BUFFER_POP(&peer->update_log);
            }
        }

        //
        // Handle input + append to circular buffer
        //
        struct input *input = &input_buffer[input_count];
        struct player *player = &peers[main_peer_index].player;
        input_count = (input_count + 1) % INPUT_BUFFER_LENGTH;
        memset(input->active, INPUT_NULL, sizeof(input->active));
#if !defined(STRESS)
        client_handle_input(player, input);
#else
        if (stress_init_r_frames > 0) {
            input->active[INPUT_MOVE_RIGHT] = true;
            --stress_init_r_frames;
        } else if (stress_init_u_frames > 0) {
            input->active[INPUT_MOVE_UP] = true;
            --stress_init_u_frames;
        } else if (stress_l_frames > 0) {
            input->active[INPUT_MOVE_LEFT] = true;
            --stress_l_frames;
        } else if (stress_d_frames > 0) {
            input->active[INPUT_MOVE_DOWN] = true;
            --stress_d_frames;
        } else if (stress_r_frames > 0) {
            input->active[INPUT_MOVE_RIGHT] = true;
            --stress_r_frames;
        } else if (stress_u_frames > 0) {
            input->active[INPUT_MOVE_UP] = true;
            --stress_u_frames;
        } else {
            stress_l_frames = (stress_scale * stress_n);
            stress_d_frames = (stress_scale * stress_n);
            stress_r_frames = (stress_scale * stress_n);
            stress_u_frames = (stress_scale * stress_n);
        }
#endif

        if (input->active[INPUT_QUIT])
            running = false;

        //
        // Do game update and send to server
        //
        if (connected) {
            struct client_header header = {
                .type = CLIENT_PACKET_UPDATE,
                .sim_tick = sim_tick,
            };

            struct client_packet_update update = {
                .input = *input,
            };

            new_packet(&output_buffer);
            APPEND(&output_buffer, &header);
            APPEND(&output_buffer, &update);

            // Predictive move
            move(&map, player, input, dt);
        }

        if (sim_tick % NET_PER_SIM_TICKS == 0) {
            const size_t size = (intptr_t) output_buffer.top - (intptr_t) output_buffer.base;
            if (size > sizeof(struct client_batch_header)) {
                struct client_batch_header *batch = (void *) output_buffer.base;
                batch->net_tick = net_tick;
                batch->adjustment_iteration = adjustment_iteration;
                ENetPacket *packet = enet_packet_create(output_buffer.base, size, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(peer, 0, packet);
                output_buffer.top = output_buffer.base;

                {
                    struct client_batch_header batch = {0};
                    APPEND(&output_buffer, &batch);
                }
            }
        }

#if !defined(STRESS)
        // Render
        BeginDrawing();
        ClearBackground(RAYWHITE);
        if (connected) {
            struct player *player = &peers[main_peer_index].player;
            draw_water(t);
            draw_map(&map);
            draw_player(player, 50.0f);
            {
                const i32 x = GetMouseX();
                const i32 y = GetMouseY();
                const i32 size = 20;
                const i32 thickness = 2;
                DrawRectangle(x - size/2,      y - thickness/2, size, thickness, BLACK);
                DrawRectangle(x - thickness/2, y - size/2,      thickness, size, BLACK);
            }
            for (u8 i = 0; i < num_peers; ++i) {
                if (i == main_peer_index)
                    continue;
                struct player *player = &peers[i].player;
                draw_player(player, 20.0f);
            }

            DrawText("client", 10, 10, 20, BLACK);
            if (sim_tick % FPS == 0) {
                fps = 1.0f / ((f32)time_nanoseconds(&frame_delta)/(f32)NANOSECS_PER_SEC);
            }
            if (!isinf(fps))
                DrawText(TextFormat("fps: %.0f", fps), 10, 30, 20, GRAY);
        }
        EndDrawing();
#endif

        // End frame
        if (adjustment > 0) {
            // Adjustment when behind of server
            // we want to process frames as fast as possible,
            // so no sleeping.
            if (sim_tick - pre_adjustment_sim_tick >= NET_PER_SIM_TICKS) {
                --adjustment;
                pre_adjustment_sim_tick = sim_tick;
            }
        } else {
            // Only sleep remaining frame time if we aren't fast forwarding
            time_current(&frame_end);
            time_subtract(&frame_delta, &frame_end, &frame_start);
            if (time_less_than(&frame_delta, &frame_desired)) {
                time_subtract(&frame_diff, &frame_desired, &frame_delta);
                nanosleep(&frame_diff, NULL);
            }
        }

        if (sim_tick % NET_PER_SIM_TICKS == 0)
            net_tick++;
        sim_tick++;
        t += dt;
    }

    enet_peer_disconnect(peer, 0);

    // Disconnect
    bool disconnected = false;
    while (enet_host_service(client, &event, 1000) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            puts("Disconnection succeeded.");
            disconnected = true;
            break;
        }
    }

    // Drop connection, since disconnection didn't successed
    if (!disconnected) {
        enet_peer_reset(peer);
    }

    enet_host_destroy(client);
    enet_deinitialize();

#if !defined(STRESS)
    CloseWindow();
#endif

    return 0;
}
