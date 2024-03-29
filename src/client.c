// Third party includes
#include <raylib.h>
#include "windows_garbage.h.inc"
#define ENET_IMPLEMENTATION
#include "enet.h"

// Our includes
#include "packet.h"
#include "common.h"
#include "random.h"
#include "color.h"
#include "game.h"
#include "audio.h"
#include "draw.h"

// stdlib
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#define PACKET_LOG_SIZE 2048
#define OUTPUT_BUFFER_SIZE 2048
#define INPUT_BUFFER_LENGTH 512
#define UPDATE_LOG_BUFFER_SIZE 512

//
// Client state
//

bool running = true;
bool connected = false;

typedef enum MenuState {
    START,
    CONNECTING,
    LOBBY,
    GAME,
} MenuState;

struct camera camera = {0};

//
// Network
//

const u64 initial_server_net_tick_offset = 5;

struct peer_auth_buffer {
    struct server_packet_peer_auth data[UPDATE_LOG_BUFFER_SIZE];
    u64 bottom;
    u64 used;
};

struct client_peer {
    PlayerId id;
    struct peer_auth_buffer auth_buffer;
};

static inline void new_packet(struct byte_buffer *output_buffer) {
    struct client_batch_header *batch = (void *) output_buffer->base;
    assert(batch->num_packets < UINT16_MAX);
    ++batch->num_packets;
}

//
// Game
//

void client_handle_input(struct player *p, struct input *input) {
    if (IsKeyPressed(KEY_M))
        input->active[INPUT_MUTE] = true;
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
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        input->active[INPUT_SHOOT_PRESSED] = true;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        input->active[INPUT_SHOOT_HELD] = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        input->active[INPUT_SHOOT_RELEASED] = true;
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        input->active[INPUT_ZOOM] = true;
    if (IsKeyPressed(KEY_F))
        input->active[INPUT_FULLSCREEN] = true;
    if (IsKeyPressed(KEY_Q))
        input->active[INPUT_SWITCH_WEAPON] = true;
    if (IsKeyDown(KEY_ESCAPE))
        input->active[INPUT_QUIT] = true;

    v2 v = screen_to_world(camera, (Vector2) {GetMouseX(), GetMouseY()});
    input->look = v2sub(v, p->pos);

    if (v2iszero(input->look)) {
        input->look.x = 1;
        input->look.y = 0;
    }
}

static Vector2 old_window_size = {0};

static void game(ENetHost *client, ENetPeer *peer, struct byte_buffer output_buffer) {
    struct graph graph = graph_new(2*FPS);

    struct timespec frame_start = {0},
                    frame_end   = {0},
                    frame_delta = {0},
                    frame_diff  = {0};

    f32 t = 0.0f;

    u8 input_count = 0;
    struct input input_buffer[INPUT_BUFFER_LENGTH] = {0};

    struct game game = {
        .map = map,
    };

    HashMap(struct client_peer, MAX_CLIENTS) peer_map = {0};
    PlayerId main_player_id;

    i8 adjustment = 0;
    u8 adjustment_iteration = 0;
    i32 total_adjustment = 0;

    struct frame frame = {
        .desired_delta = NANOSECONDS(1) / (f32) FPS,
        .dt = 1.0f / (f32) FPS,
    };

    ENetEvent event = {0};

    bool mute = false;

    struct frame_debug_data frame_debug = {0};

    while (running) {
        // Begin frame
        const u64 frame_start = time_current();

        bool run_network_tick = frame.simulation_tick % NET_PER_SIM_TICKS == 0;
        bool sleep_this_frame = true;

        // Collect frame debug data
        if (frame.simulation_tick % FPS == 0) {
            frame_debug.total_frame_start = time_current();
            // Save incoming data before each frame so we can compute
            // bandwidth, for somer reason peer->[incoming|outoing]Bandwidth
            // is always zero.
            frame_debug.incoming_data_total_start = peer->incomingDataTotal;
            frame_debug.outgoing_data_total_start = peer->outgoingDataTotal;
        }

        if (adjustment > 0) {
            // If we still have a positive adjustment, don't sleep this
            // frame.
            sleep_this_frame = false;
            --adjustment;
        }
        if (run_network_tick) {

            // Fetch network data
            while (enet_host_service(client, &event, 0) > 0) {
                switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    // Packet batch header
                    struct byte_buffer net_input_buffer = byte_buffer_init(event.packet->data, event.packet->dataLength);
                    struct server_batch_header *batch;
                    POP(&net_input_buffer, &batch);

                    if (batch->adjustment != 0 && adjustment_iteration == batch->adjustment_iteration) {
                        printf("We have %d adjustment (%u)\n", batch->adjustment, adjustment_iteration);
                        adjustment = batch->adjustment;
                        total_adjustment += adjustment;

                        ++adjustment_iteration;

                        if (adjustment < 0) {
                            // If we have a negative adjustment, we're ahead of the server and can deal
                            // with this immediately by just sleeping for the amount we're ahead.
                            time_nanosleep((-adjustment)*frame.desired_delta);
                            adjustment = 0;
                        } else if (adjustment > 0) {
                            // If we have a positive adjustment we're behind, so
                            // disable sleeping for this frame
                            sleep_this_frame = false;
                            --adjustment;
                        }
                    }

                    for (u16 packet = 0; packet < batch->num_packets; ++packet) {
                        struct server_header *header;
                        POP(&net_input_buffer, &header);

                        // Packet payload
                        switch (header->type) {
                        case SERVER_PACKET_GREETING: {
                            struct server_packet_greeting *greeting;
                            POP(&net_input_buffer, &greeting);

                            frame.network_tick = greeting->initial_net_tick + initial_server_net_tick_offset;
                            frame.simulation_tick = frame.network_tick * NET_PER_SIM_TICKS;

                            struct player *player = NULL;
                            HashMapInsert(game.player_map, greeting->id, player);
                            main_player_id = player->id;

                            struct client_peer* peer = NULL;
                            HashMapInsert(peer_map, greeting->id, peer);
                            peer->id = player->id;

                            connected = true;
                        } break;

                        case SERVER_PACKET_PEER_GREETING: {
                            struct server_packet_peer_greeting *greeting;
                            POP(&net_input_buffer, &greeting);

                            struct player *player = NULL;
                            HashMapInsert(game.player_map, greeting->id, player);

                            struct client_peer *peer = NULL;
                            HashMapInsert(peer_map, player->id, peer);
                            peer->id = player->id;
                        } break;

                        case SERVER_PACKET_PLAYER_SPAWN: {
                            struct server_packet_player_spawn *spawn;
                            POP(&net_input_buffer, &spawn);

                            struct player *player = NULL;
                            HashMapLookup(game.player_map, spawn->player.id, player);
                            *player = spawn->player;
                        } break;

                        case SERVER_PACKET_NADE: {
                            struct server_packet_nade *nade_packet;
                            POP(&net_input_buffer, &nade_packet);
                            // Skip nades we've already taken care of locally
                            if (nade_packet->nade.player_id_from != main_player_id) {
                                ListInsert(game.nade_list, nade_packet->nade);
                            }
                        } break;

                        case SERVER_PACKET_SOUND: {
                            struct server_packet_sound *sound_packet;
                            POP(&net_input_buffer, &sound_packet);
                            // Skip nades we've already taken care of locally
                            if (sound_packet->sound.player_id_from != main_player_id) {
                                ListInsert(game.sound_list, sound_packet->sound);
                            }
                        } break;

                        case SERVER_PACKET_STEP: {
                            struct server_packet_step *step_packet;
                            POP(&net_input_buffer, &step_packet);
                            // Skip nades we've already taken care of locally
                            if (step_packet->step.player_id_from != main_player_id) {
                                ListInsert(game.step_list, step_packet->step);
                            }
                        } break;


                        case SERVER_PACKET_HITSCAN: {
                            struct server_packet_hitscan *hitscan_packet;
                            POP(&net_input_buffer, &hitscan_packet);
                            // Skip hitscans we've already taken care of locally
                            if (hitscan_packet->hitscan.player_id_from != main_player_id) {
                                ListInsert(game.hitscan_list, hitscan_packet->hitscan);
                            }
                        } break;

                        case SERVER_PACKET_AUTH: {
                            struct server_packet_auth *auth;
                            POP(&net_input_buffer, &auth);

                            assert(auth->sim_tick <= frame.simulation_tick);
                            u64 diff = frame.simulation_tick - auth->sim_tick - 1;
                            assert(diff < INPUT_BUFFER_LENGTH);

                            struct player *player = NULL;
                            HashMapLookup(game.player_map, main_player_id, player);

                            // Gets the input for the sim_tick after the sim_tick we recieved auth data for
                            // NOTE(anjo): We might have to actually use older game states here, this could
                            // cause WEIRD problems.
                            struct game old_game = game;
                            struct player old_player = auth->player;
                            u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                            for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                                struct input *old_input = &input_buffer[old_index];
                                update_player(&old_game, &old_player, old_input, frame.dt);
                                collect_and_resolve_static_collisions_for_player(&old_game, &old_player);
                            }

                            if (!v2equal(player->pos, old_player.pos)) {
                                printf("  Server disagreed! {%f, %f} vs {%f, %f}\n", player->pos.x, player->pos.y, old_player.pos.x, old_player.pos.y);
                                *player = auth->player;
                            }
                        } break;

                        case SERVER_PACKET_PEER_AUTH: {
                            struct server_packet_peer_auth *peer_auth;
                            POP(&net_input_buffer, &peer_auth);

                            struct client_peer *peer = NULL;
                            HashMapLookup(peer_map, peer_auth->player.id, peer);

                            CIRCULAR_BUFFER_APPEND(&peer->auth_buffer, *peer_auth);
                        } break;

                        case SERVER_PACKET_PLAYER_KILL: {
                            struct server_packet_player_kill *kill;
                            POP(&net_input_buffer, &kill);

                            struct player *p = NULL;
                            HashMapLookup(game.player_map, kill->player_id, p);
                            p->health = 0.0f;
                            ListInsert(game.sound_list, ((struct spatial_sound){kill->player_id, SOUND_PLAYER_KILL, p->pos}));
                        } break;

                        case SERVER_PACKET_DROPPED: {
                            printf("we have a dropped packet!\n");
                        } break;

                        case SERVER_PACKET_PEER_DISCONNECTED: {
                            struct server_packet_peer_disconnected *disc;
                            POP(&net_input_buffer, &disc);

                            printf("%d disconnected!\n", disc->player_id);
                            HashMapRemove(game.player_map, disc->player_id);
                            HashMapRemove(peer_map, disc->player_id);
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

                case ENET_EVENT_TYPE_CONNECT:
                    break;

                case ENET_EVENT_TYPE_NONE:
                    break;
                }

                enet_packet_destroy(event.packet);
            }
        }

        //
        // Loop over all peers, and apply auth data
        //

        // active_tick is the tick we're applying peer data from,
        // this is always less than the current simulation tick.
        //u64 active_tick = frame.simulation_tick + 2*total_adjustment;
        HashMapForEach(peer_map, struct client_peer, peer) {
            if (!HashMapExists(peer_map, peer) || peer->id == main_player_id || peer->auth_buffer.used == 0)
                continue;

            struct server_packet_peer_auth *entry = &peer->auth_buffer.data[peer->auth_buffer.bottom];
            //printf("we should get here: %u %u\n", active_tick, entry->sim_tick);
            //if (active_tick < entry->sim_tick)
            //    continue;

            struct player *player = NULL;
            HashMapLookup(game.player_map, peer->id, player);
            *player = entry->player;

            CIRCULAR_BUFFER_POP(&peer->auth_buffer);
        }

        struct player *player = NULL;
        if (main_player_id != HASH_MAP_INVALID_HASH)
            HashMapLookup(game.player_map, main_player_id, player);

        //
        // Handle input + append to circular buffer
        //
        if (connected) {
            assert(player != NULL);

            struct input *input = &input_buffer[input_count];
            input_count = (input_count + 1) % INPUT_BUFFER_LENGTH;
            memset(input->active, INPUT_NULL, sizeof(input->active));

            client_handle_input(player, input);

            if (input->active[INPUT_MUTE])
                mute = !mute;

            if (input->active[INPUT_FULLSCREEN])
                ToggleFullscreen();

            if (input->active[INPUT_QUIT])
                running = false;

            //
            // Do game update and send to server, assuming we
            // are connected and the player is alive
            //

            if (player->health > 0.0f) {
                struct client_header header = {
                    .type = CLIENT_PACKET_UPDATE,
                    .sim_tick = frame.simulation_tick,
                };

                struct client_packet_update update = {
                    .input = *input,
                };

                new_packet(&output_buffer);
                APPEND(&output_buffer, &header);
                APPEND(&output_buffer, &update);

                // Predictive move
                update_player(&game, player, input, frame.dt);
                collect_and_resolve_static_collisions(&game);
            }
        }

        update_projectiles(&game, frame.dt);

        // Play queued sounds
        if (connected) {
            assert(player != NULL);
            if (!mute) {
                ForEachList(game.sound_list, struct spatial_sound, spatial_sound) {
                    audio_play_spatial_sound(spatial_sound->sound, spatial_sound->pos, player->pos, player->look);
                }
            }
            ListClear(game.sound_list);
        }

        if (run_network_tick) {
            const size_t size = (intptr_t) output_buffer.top - (intptr_t) output_buffer.base;
            if (size > sizeof(struct client_batch_header)) {
                struct client_batch_header *batch = (void *) output_buffer.base;
                batch->net_tick = frame.network_tick;
                batch->adjustment_iteration = adjustment_iteration;
                ENetPacket *packet = enet_packet_create(output_buffer.base, size, ENET_PACKET_FLAG_UNSEQUENCED);
                enet_peer_send(peer, 0, packet);
                output_buffer.top = output_buffer.base;

                {
                    struct client_batch_header batch = {0};
                    APPEND(&output_buffer, &batch);
                }
            }
        }

        // Render
        BeginDrawing();
        ClearBackground(BLACK);
        if (connected) {
            assert(player != NULL);

            camera.offset = (v2) {GetRenderWidth()/2, GetRenderHeight()/2};
            camera.target = player->pos;
            draw_game(camera, &game, main_player_id, frame.dt, t);

            DrawText("client", 10, 10, 20, BLACK);

            int y = 30;
            DrawText(TextFormat("fps: %.0f (%.0f)", frame_debug.fps, 1000000000.0f/((f32)frame_debug.total_delta)), 10, y, 20, GRAY); y += 20;
            DrawText(TextFormat("ping: %u", peer->roundTripTime), 10, y, 20, GRAY); y += 20;
            DrawText(TextFormat("in  bandwidth: %u bytes/s", frame_debug.incoming_bandwidth), 10, y, 20, GRAY); y += 20;
            DrawText(TextFormat("out bandwidth: %u bytes/s", frame_debug.outgoing_bandwidth), 10, y, 20, GRAY); y += 20;
            DrawText(TextFormat("total adjustment: %d", total_adjustment), 10, y, 20, GRAY); y += 20;

            //graph_append(&graph, v2len(player->velocity));
            draw_all_debug_v2s(camera);
            //draw_graph(&graph,
            //           (v2) {10, 80},
            //           (v2) {300, 200},
            //           (v2) {10, 10});
        }
        EndDrawing();

        // End frame
        if (sleep_this_frame) {
            // Only sleep remaining frame time if we aren't fast forwarding
            const u64 frame_end = time_current();
            frame.delta = frame_end - frame_start;
            if (frame.delta < frame.desired_delta) {
                time_nanosleep(frame.desired_delta - frame.delta);
            }

            // Collect frame debug data
            if (frame.simulation_tick % FPS == 0) {
                frame_debug.fps = 1.0f / ((f32) frame.delta / (f32) NANOSECONDS(1));
                frame_debug.incoming_bandwidth = FPS * (peer->incomingDataTotal - frame_debug.incoming_data_total_start);
                frame_debug.outgoing_bandwidth = FPS * (peer->outgoingDataTotal - frame_debug.outgoing_data_total_start);
                frame_debug.total_delta = time_current() - frame_debug.total_frame_start;
            }
        }

        if (run_network_tick)
            ++frame.network_tick;
        ++frame.simulation_tick;
        t += frame.dt;
    }

    graph_free(&graph);
}

#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
#else
int main(int argc, char **argv) {
#endif
    if (enet_initialize() != 0) {
        fprintf(stderr, "An error occurred while initializing ENet.\n");
        return EXIT_FAILURE;
    }

    draw_init();
    audio_init();
    time_init();

    struct byte_buffer output_buffer = byte_buffer_alloc(OUTPUT_BUFFER_SIZE);
    APPEND(&output_buffer, &(struct client_batch_header){0});

    MenuState menu_state = START;

    // Text input
    char input[16] = {0};
    u8 input_size = 0;
    u64 delete_start = 0;
    u64 cursor_blink_start = 0;
    bool draw_cursor = true;

    // If we have a first argument, assume it's an ip
    // and connect to it, skipping the intial input
    // menu state.
#if !defined(_WIN32)
    if (argc > 1) {
        strncpy(input, argv[1], ARRLEN(input));
        menu_state = CONNECTING;
    }
#endif

    // Net stuff
    ENetHost *client = {0};
    client = enet_host_create(NULL, 1, 1, 0, 0);
    if (client == NULL) {
        fprintf(stderr, "An error occurred while trying to create an ENet client host.\n");
        exit(EXIT_FAILURE);
    }
    ENetAddress address = {0};
    ENetEvent event = {0};
    ENetPeer *peer = {0};
    address.port = 9053;

    while (!WindowShouldClose()) {
        switch (menu_state) {
        case START: {
            const int w = GetRenderWidth();
            const int h = GetRenderHeight();

            BeginDrawing();
            ClearBackground(BLACK);

            int key = GetCharPressed();
            if (key >= 32 && key <= 125 && input_size < ARRLEN(input)-1) {
                input[input_size++] = key;
                input[input_size] = '\0';
            }

            if (IsKeyDown(KEY_BACKSPACE) && input_size > 0) {
                const u64 current = time_current();
                if (current - delete_start >= 50000000) {
                    delete_start = current;
                    input[--input_size] = '\0';
                }
            }

            DrawText(TextFormat("ip: %s", input), 0.1f * (float)w, 0.4f * (float)h, 20, WHITE);
            const char *str = TextFormat("ip: %s", input);

            {
                const u64 current = time_current();
                if (current - cursor_blink_start >= 1000000000) {
                    cursor_blink_start = current;
                    draw_cursor = !draw_cursor;
                }
            }

            if (draw_cursor) {
                DrawRectangle(0.11f * (float)w + MeasureText(str, 20),
                              0.4f * (float)h,
                              10,
                              20,
                              WHITE);
            }

            if (IsKeyPressed(KEY_ENTER)) {
                menu_state = CONNECTING;
            }

            DrawText("Press [ENTER] to connect", 0.1f * (float)w, 0.50f * (float)h, 20, WHITE);
            DrawText("Press [ESC] to quit",      0.1f * (float)w, 0.55f * (float)h, 20, WHITE);

            EndDrawing();
        } break;

        case CONNECTING: {
            const int w = GetRenderWidth();
            const int h = GetRenderHeight();

            // Draw something so the user know something is going on
            // even tho the program will hang when connecting.
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText("Connecting...", 0.1f * (float)w, 0.4f * (float)h, 20, WHITE);
            EndDrawing();

            enet_address_set_host(&address, input);
            peer = enet_host_connect(client, &address, 2, 0);
            if (peer == NULL) {
                fprintf(stderr, "No available peers for initiating an ENet connection.\n");
                exit(EXIT_FAILURE);
            }
            /* Wait up to 5 seconds for the connection attempt to succeed. */
            if (enet_host_service(client, &event, 5000) > 0 &&
                event.type == ENET_EVENT_TYPE_CONNECT) {
                menu_state = GAME;
            } else {
                // In the event that connection failed let's just draw some
                // text and wait a second
                BeginDrawing();
                ClearBackground(BLACK);
                DrawText("Failed to connect to server", 0.1f * (float)w, 0.4f * (float)h, 20, RED);
                EndDrawing();
                time_nanosleep(NANOSECONDS(2));

                /* Either the 5 seconds are up or a disconnect event was */
                /* received. Reset the peer in the event the 5 seconds   */
                /* had run out without any significant event.            */
                enet_peer_reset(peer);
                menu_state = START;
            }
        } break;

        case LOBBY: {
        } break;

        case GAME: {
            game(client, peer, output_buffer);
        } break;

        }
    }

    enet_peer_disconnect(peer, 0);

    // Disconnect
    bool disconnected = false;
    while (enet_host_service(client, &event, 100) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            puts("Disconnection succeeded.");
            disconnected = true;
            break;
        default:
            break;
        }
    }

    // Drop connection, since disconnection didn't successed
    if (!disconnected) {
        enet_peer_reset(peer);
    }

    enet_host_destroy(client);
    enet_deinitialize();

    audio_deinit();
    draw_deinit();
    byte_buffer_free(&output_buffer);

    return 0;
}
