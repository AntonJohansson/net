#define ENET_IMPLEMENTATION
#include "enet.h"

#include "packet.h"
#include "common.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "random.h"
#include <raylib.h>
#include "color.h"
#include "draw.h"

#define WIDTH 800
#define HEIGHT 600

#define PACKET_LOG_SIZE 2048
#define OUTPUT_BUFFER_SIZE 2048
#define INPUT_BUFFER_LENGTH 512
#define UPDATE_LOG_BUFFER_SIZE 512

const u64 initial_server_net_tick_offset = 5;

bool running = true;

bool connected = false;

struct frame {
    u64 desired_delta;
    u64 delta;
    u64 simulation_tick;
    u64 network_tick;
    f32 dt;
};

struct peer_auth_buffer {
    struct server_packet_peer_auth data[UPDATE_LOG_BUFFER_SIZE];
    u64 bottom;
    u64 used;
};

struct peer {
    bool connected;
    struct player *player;
    u64 player_index;
    struct peer_auth_buffer auth_buffer;
};

static inline void peer_disconnect(struct peer *p) {
    p->player->occupied = false;
    memset(p, 0, sizeof(struct peer));
}

static inline void new_packet(struct byte_buffer *output_buffer) {
    struct client_batch_header *batch = (void *) output_buffer->base;
    assert(batch->num_packets < UINT16_MAX);
    ++batch->num_packets;
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
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        input->active[INPUT_SHOOT] = true;
    if (IsKeyDown(KEY_Q))
        input->active[INPUT_QUIT] = true;

    Vector2 v = screen_to_world((Vector2) {GetMouseX(), GetMouseY()});
    input->x = v.x - p->pos.x;
    input->y = v.y - p->pos.y;
}


int main(int argc, char **argv) {
    assert(argc > 1);
    const char *ip = argv[1];

    struct byte_buffer output_buffer = byte_buffer_alloc(OUTPUT_BUFFER_SIZE);

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
    enet_address_set_host(&address, ip);
    address.port = 9053;

    peer = enet_host_connect(client, &address, 2, 0);
    if (peer == NULL) {
        fprintf(stderr, "No available peers for initiating an ENet connection.\n");
        exit(EXIT_FAILURE);
    }
    /* Wait up to 5 seconds for the connection attempt to succeed. */
    if (enet_host_service(client, &event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        puts("Connection to server succeeded.");
    } else {
        /* Either the 5 seconds are up or a disconnect event was */
        /* received. Reset the peer in the event the 5 seconds   */
        /* had run out without any significant event.            */
        enet_peer_reset(peer);
        puts("Connection to server failed.");
        exit(EXIT_FAILURE);
    }

    InitWindow(WIDTH, HEIGHT, "floating");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    HideCursor();

    struct graph graph = graph_new(2*FPS);

    struct timespec frame_start = {0},
                    frame_end   = {0},
                    frame_delta = {0},
                    frame_diff  = {0};

    f32 t = 0.0f;
    f32 fps = 0.0f;

    u8 input_count = 0;
    struct input input_buffer[INPUT_BUFFER_LENGTH] = {0};

    struct game game = {
        .map = map,
    };

    struct peer peers[MAX_CLIENTS] = {0};
    u8 num_peers = 0;
    u8 main_peer_index;

    i8 adjustment = 0;
    u8 adjustment_iteration = 0;
    i32 total_adjustment = 0;

    struct frame frame = {
        .desired_delta = NANOSECONDS(1) / (f32) FPS,
        .dt = 1.0f / (f32) FPS,
    };

    bool automove = false;

    while (running) {
        // Begin frame
        const u64 frame_start = time_current();

        bool run_network_tick = frame.simulation_tick % NET_PER_SIM_TICKS == 0;
        bool sleep_this_frame = true;

        if (adjustment < 0) {
            time_nanosleep(frame.desired_delta);
            ++adjustment;
        } else if (adjustment > 0) {
            sleep_this_frame = false;
            printf("not sleeping\n");
            --adjustment;
        }
        if (run_network_tick) {

            // Fetch network data
            while (enet_host_service(client, &event, 0) > 0) {
                switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    // Packet batch header
                    const u8 *p = event.packet->data;
                    struct server_batch_header *batch = (struct server_batch_header *) p;
                    p += sizeof(struct server_batch_header);

                    if (batch->adjustment != 0 && adjustment_iteration == batch->adjustment_iteration) {
                        adjustment = batch->adjustment;
                        total_adjustment += adjustment;
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

                            frame.network_tick = greeting->initial_net_tick + initial_server_net_tick_offset;
                            frame.simulation_tick = frame.network_tick * NET_PER_SIM_TICKS;
                            main_peer_index = greeting->peer_index;
                            assert(!peers[main_peer_index].connected);
                            peers[main_peer_index].connected = true;

                            struct player *player = NULL;
                            u64 player_index = 0;
                            for (; player_index < MAX_CLIENTS; ++player_index) {
                                player = &game.players[player_index];
                                if (!player->occupied)
                                    break;
                            }

                            assert(player != NULL);
                            player->occupied = true;
                            player->pos = greeting->initial_pos;
                            player->hue = 50.0f;
                            player->health = 100.0f;

                            peers[main_peer_index].player = player;
                            peers[main_peer_index].player_index = player_index;
                            ++num_peers;
                            connected = true;
                        } break;

                        case SERVER_PACKET_PEER_GREETING: {
                            struct server_packet_peer_greeting *greeting = (struct server_packet_peer_greeting *) p;
                            p += sizeof(struct server_packet_peer_greeting);

                            u8 peer_index = greeting->peer_index;

                            struct player *player = NULL;
                            for (u32 i = 0; i < MAX_CLIENTS; ++i) {
                                player = &game.players[i];
                                if (!player->occupied)
                                    break;
                            }

                            assert(player != NULL);
                            player->occupied = true;
                            player->pos = greeting->initial_pos;
                            player->hue = 20.0f;
                            player->health = greeting->health;

                            peers[peer_index].player = player;
                            ++num_peers;
                        } break;

                        case SERVER_PACKET_AUTH: {
                            struct server_packet_auth *auth = (struct server_packet_auth *) p;
                            p += sizeof(struct server_packet_auth);

                            assert(auth->sim_tick <= frame.simulation_tick);
                            u64 diff = frame.simulation_tick - auth->sim_tick - 1;
                            assert(diff < INPUT_BUFFER_LENGTH);

                            struct player *player = peers[main_peer_index].player;

                            // Gets the input for the sim_tick after the sim_tick we recieved auth data for
                            struct game old_game = game;
                            struct player old_player = auth->player;
                            u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                            for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                                struct input *old_input = &input_buffer[old_index];
                                move(&old_game, &old_player, old_input, frame.dt, peers[main_peer_index].player_index, true);
                            }

                            if (!v2equal(player->pos, old_player.pos)) {
                                {
                                    printf("  Replaying %lu inputs from %lu+1 to %lu-1\n", diff, auth->sim_tick, frame.simulation_tick);
                                    printf("    Starting from {%f, %f}\n", auth->player.pos.x, auth->player.pos.y);
                                    printf("    Should match {%f, %f}\n", player->pos.x, player->pos.y);

                                    struct game old_game = game;
                                    struct player tmp_player = auth->player;
                                    u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                                    for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                                        struct input *old_input = &input_buffer[old_index];
                                        move(&old_game, &tmp_player, old_input, frame.dt, peers[main_peer_index].player_index, true);
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
                            CIRCULAR_BUFFER_APPEND(&peer->auth_buffer, *peer_auth);
                        } break;

                        case SERVER_PACKET_DROPPED: {
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

                case ENET_EVENT_TYPE_CONNECT:
                    break;

                case ENET_EVENT_TYPE_NONE:
                    break;
                }
                enet_packet_destroy(event.packet);
            }
        }

        u64 active_tick = frame.simulation_tick + 2*total_adjustment;
        for (u8 i = 0; i < num_peers; ++i) {
            if (i == main_peer_index)
                continue;
            struct peer *peer = &peers[i];
            if (peer->auth_buffer.used > 0) {
                struct server_packet_peer_auth *entry = &peer->auth_buffer.data[peer->auth_buffer.bottom];

                if (active_tick < entry->sim_tick)
                    continue;

                *peer->player = entry->player;

                CIRCULAR_BUFFER_POP(&peer->auth_buffer);
            }
        }

        //
        // Handle input + append to circular buffer
        //
        struct input *input = &input_buffer[input_count];
        struct player *player = peers[main_peer_index].player;
        input_count = (input_count + 1) % INPUT_BUFFER_LENGTH;
        memset(input->active, INPUT_NULL, sizeof(input->active));
        if (connected) {
            client_handle_input(player, input);
        }

        //if (input->active[INPUT_QUIT])
        //    running = false;
        if (input->active[INPUT_QUIT]) {
            automove = !automove;
        }

        if (automove) {
            if (frame.simulation_tick % 100 < 50)
                input->active[INPUT_MOVE_LEFT] = true;
            else
                input->active[INPUT_MOVE_RIGHT] = true;
        }

        //
        // Do game update and send to server
        //
        if (connected) {
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
            move(&game, player, input, frame.dt, peers[main_peer_index].player_index, false);
        }

        if (run_network_tick) {
            const size_t size = (intptr_t) output_buffer.top - (intptr_t) output_buffer.base;
            if (size > sizeof(struct client_batch_header)) {
                struct client_batch_header *batch = (void *) output_buffer.base;
                batch->net_tick = frame.network_tick;
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

        // Render
        BeginDrawing();
        ClearBackground(RAYWHITE);
        if (connected) {
            draw_game(&game, t);

            DrawText("client", 10, 10, 20, BLACK);
            if (frame.simulation_tick % FPS == 0) {
                fps = 1.0f / ((f32) frame.delta / (f32) NANOSECONDS(1));
            }
            if (!isinf(fps)) {
                DrawText(TextFormat("fps: %.0f", fps), 10, 30, 20, GRAY);
                DrawText(TextFormat("ping: %f", frame.dt * 1000.0f * NET_PER_SIM_TICKS * (f32) (-total_adjustment)), 10, 50, 20, GRAY);
            }

            graph_append(&graph, v2len(player->velocity));
            draw_all_debug_v2s();
            draw_graph(&graph,
                       (v2) {10, 80},
                       (v2) {300, 200},
                       (v2) {10, 10});
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
        }

        if (run_network_tick)
            ++frame.network_tick;
        ++frame.simulation_tick;
        t += frame.dt;
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

    CloseWindow();
    graph_free(&graph);
    byte_buffer_free(&output_buffer);

    return 0;
}
