#define ENET_IMPLEMENTATION
#include "enet.h"

#include "packet.h"
#include "common.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include <raylib.h>

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

void client_handle_input(struct input *input) {
    memset(input->active, INPUT_NULL, sizeof(input->active));
    if (IsKeyDown(KEY_W))
        input->active[INPUT_MOVE_UP] = true;
    if (IsKeyDown(KEY_A))
        input->active[INPUT_MOVE_LEFT] = true;
    if (IsKeyDown(KEY_S))
        input->active[INPUT_MOVE_DOWN] = true;
    if (IsKeyDown(KEY_D))
        input->active[INPUT_MOVE_RIGHT] = true;
    if (IsKeyDown(KEY_Q))
        input->active[INPUT_QUIT] = true;
}

int main() {
    signal(SIGINT, inthandler);

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

    InitWindow(WIDTH, HEIGHT, "client");

    struct timespec frame_start = {0},
                    frame_end   = {0},
                    frame_delta = {0};

    struct timespec frame_desired = {
        .tv_sec = 0,
        .tv_nsec = NANOSECS_PER_SEC / FPS,
    };

    const f32 dt = time_nanoseconds(&frame_desired);

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

    while (running) {
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
                            main_peer_index = greeting->peer_index;
                            assert(!peers[main_peer_index].connected);
                            peers[main_peer_index].connected = true;
                            peers[main_peer_index].player.x = greeting->initial_x;
                            peers[main_peer_index].player.y = greeting->initial_y;
                            ++num_peers;
                            connected = true;
                        } break;
                        case SERVER_PACKET_PEER_GREETING: {
                            struct server_packet_peer_greeting *greeting = (struct server_packet_peer_greeting *) p;
                            p += sizeof(struct server_packet_peer_greeting);

                            u8 peer_index = greeting->peer_index;
                            peers[peer_index].player.x = greeting->initial_x;
                            peers[peer_index].player.y = greeting->initial_y;
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
                            struct player old_player = {
                                .x = auth->x,
                                .y = auth->y,
                            };
                            u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                            for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                                struct input *old_input = &input_buffer[old_index];
                                move(&old_player, old_input, dt);
                            }

                            if (!f32_equal(player->x, old_player.x) && !f32_equal(player->y, old_player.y)) {
                                {
                                    printf("  Replaying %d inputs from %d+1 to %d-1\n", diff, auth->sim_tick, sim_tick);
                                    printf("    Starting from {%f, %f}\n", auth->x, auth->y);
                                    printf("    Should match {%f, %f}\n", player->x, player->y);

                                    struct player tmp_player = {
                                        .x = auth->x,
                                        .y = auth->y,
                                    };
                                    u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                                    for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                                        struct input *old_input = &input_buffer[old_index];
                                        move(&tmp_player, old_input, dt);
                                        printf("    -> {%f, %f}\n", tmp_player.x, tmp_player.y);
                                    }
                                }

                                printf("  Server disagreed! {%f, %f} vs {%f, %f}\n", player->x, player->y, old_player.x, old_player.y);
                                player->x = old_player.x;
                                player->y = old_player.y;
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

                peer->player.x = entry->x;
                peer->player.y = entry->y;

                CIRCULAR_BUFFER_POP(&peer->update_log);
            }
        }

        //
        // Handle input + append to circular buffer
        //
        struct input *input = &input_buffer[input_count];
        input_count = (input_count + 1) % INPUT_BUFFER_LENGTH;
        client_handle_input(input);

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

            struct player *player = &peers[main_peer_index].player;

            // Predictive move
            move(player, input, dt);
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

        // Render
        BeginDrawing();
        ClearBackground(RAYWHITE);
        if (connected) {
            DrawText("client", 10, 10, 20, BLACK);
            struct player *player = &peers[main_peer_index].player;
            DrawCircle(player->x, player->y, 10.0f, GREEN);
            for (u8 i = 0; i < num_peers; ++i) {
                if (i == main_peer_index)
                    continue;
                DrawCircle(peers[i].player.x, peers[i].player.y, 10.0f, RED);
            }
        }
        EndDrawing();

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
                time_subtract(&frame_delta, &frame_desired, &frame_delta);
                nanosleep(&frame_delta, NULL);
            }
        }

        if (sim_tick % NET_PER_SIM_TICKS == 0)
            net_tick++;
        sim_tick++;
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
    CloseWindow();
    return 0;
}
