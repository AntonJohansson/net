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
#define OUTPUT_BUFFER_SIZE 8192
#define INPUT_BUFFER_LENGTH 16
#define UPDATE_LOG_BUFFER_SIZE 512
#define VALID_TICK_WINDOW 2

bool running = true;

//
// TODO(anjo): We should attach adjustment info to the earliest possible packet that returns to the
//             client. Currently we attach the adjustment info whenever we process the update packet.
//             This should lead to delays in getting the client synced, but shouldn't cause more problems
//             than that.
//

void inthandler(int sig) {
    (void) sig;
    running = false;
}

struct update_log_entry {
    u64 client_sim_tick;
    u64 server_net_tick;
    struct client_packet_update input_update;
};

struct update_log_buffer {
    struct update_log_entry data[UPDATE_LOG_BUFFER_SIZE];
    u64 bottom;
    u64 used;
};

struct peer {
    bool connected;
    bool update_processed;
    struct player player;
    struct update_log_buffer update_log;
    struct byte_buffer output_buffer;
    ENetPeer *enet_peer;
};

static inline void peer_disconnect(struct peer *p) {
    memset(p, 0, sizeof(struct peer));
}

static inline void new_packet(struct peer *p) {
    struct server_batch_header *batch = (void *) p->output_buffer.base;
    assert(batch->num_packets < UINT16_MAX);
    ++batch->num_packets;
}

int main() {
    signal(SIGINT, inthandler);

    if (enet_initialize() != 0) {
        printf("An error occurred while initializing ENet.\n");
        return 1;
    }

    ENetAddress address = {0};

    address.host = ENET_HOST_ANY;
    address.port = 9053;

    /* create a server */
    ENetHost *server = enet_host_create(&address, MAX_CLIENTS, 1, 0, 0);

    if (server == NULL) {
        printf("An error occurred while trying to create an ENet server host.\n");
        return 1;
    }

    ENetEvent event = {0};

    struct timespec frame_start = {0},
                    frame_end   = {0},
                    frame_delta = {0},
                    frame_diff = {0};

    struct timespec frame_desired = {
        .tv_sec = 0,
        .tv_nsec = NANOSECS_PER_SEC / FPS,
    };

    u64 sim_tick = 0;
    u64 net_tick = 0;
    const f32 dt = time_nanoseconds(&frame_desired) / (f32) NANOSECS_PER_SEC;
    f32 fps = 0.0f;

    struct peer peers[MAX_CLIENTS] = {0};
    u8 num_peers = 0;

    InitWindow(WIDTH, HEIGHT, "server");

    while (running) {
        time_current(&frame_start);

        // Handle network
        if (sim_tick % NET_PER_SIM_TICKS == 0) {
            while (enet_host_service(server, &event, 0) > 0) {
                switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    printf("A new client connected from %x:%u.\n",  event.peer->address.host, event.peer->address.port);

                    assert(num_peers < MAX_CLIENTS);
                    u8 peer_index = 0;
                    for (; peer_index < MAX_CLIENTS; ++peer_index)
                        if (!peers[peer_index].connected)
                            break;
                    ++num_peers;
                    event.peer->data = malloc(sizeof(u8));
                    *(u8 *)event.peer->data = peer_index;

                    peers[peer_index].connected = true;
                    peers[peer_index].player.pos = (v2) {0, 0};
                    peers[peer_index].enet_peer = event.peer;
                    peers[peer_index].output_buffer.base = malloc(OUTPUT_BUFFER_SIZE);
                    peers[peer_index].output_buffer.size = OUTPUT_BUFFER_SIZE;
                    peers[peer_index].output_buffer.top = peers[peer_index].output_buffer.base;

                    struct server_batch_header batch = {
                        .num_packets = 0,
                    };
                    APPEND(&peers[peer_index].output_buffer, &batch);

                    // Send greeting for peer
                    {
                        struct server_header header = {
                            .type = SERVER_PACKET_GREETING,
                        };

                        struct server_packet_greeting greeting = {
                            .initial_net_tick = net_tick,
                            .initial_pos = peers[peer_index].player.pos,
                            .peer_index = peer_index,
                        };

                        new_packet(&peers[peer_index]);
                        APPEND(&peers[peer_index].output_buffer, &header);
                        APPEND(&peers[peer_index].output_buffer, &greeting);
                    }

                    // Send greeting to all other peers
                    {
                        struct server_header header = {
                            .type = SERVER_PACKET_PEER_GREETING,
                        };

                        struct server_packet_peer_greeting greeting = {
                            .initial_pos = peers[peer_index].player.pos,
                            .peer_index = peer_index,
                        };

                        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                            if (!peers[i].connected || i == peer_index)
                                continue;
                            new_packet(&peers[i]);
                            APPEND(&peers[i].output_buffer, &header);
                            APPEND(&peers[i].output_buffer, &greeting);
                        }
                    }

                    // Send greeting to this peer about all other peers already connected
                    {
                        struct server_header header = {
                            .type = SERVER_PACKET_PEER_GREETING,
                        };

                        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                            if (!peers[i].connected || i == peer_index)
                                continue;
                            struct server_packet_peer_greeting greeting = {
                                .initial_pos = peers[i].player.pos,
                                .peer_index = i,
                            };

                            new_packet(&peers[peer_index]);
                            APPEND(&peers[peer_index].output_buffer, &header);
                            APPEND(&peers[peer_index].output_buffer, &greeting);
                        }
                    }
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    struct byte_buffer input_buffer = {
                        .base = event.packet->data,
                        .size = event.packet->dataLength,
                    };
                    input_buffer.top = input_buffer.base;
                    struct client_batch_header *batch;
                    POP(&input_buffer, &batch);

                    const u8 peer_index = *(u8 *)event.peer->data;
                    assert(peer_index >= 0 && peer_index < MAX_CLIENTS);
                    assert(peers[peer_index].connected);

                    i8 adjustment = 0;
                    i64 diff = (i64) net_tick + (VALID_TICK_WINDOW-1) - (i64) batch->net_tick;
                    if (diff < INT8_MIN || diff > INT8_MAX) {
                        printf("net_tick diff outside range of adjustment variable!\n");
                        // TODO(anjo): what do?
                        break;
                    }
                    if (diff < -(VALID_TICK_WINDOW-1) || diff > 0) {
                        // Need adjustment
                        adjustment = (i8) diff;
                    }
                    if (batch->net_tick >= net_tick) {
                        if (diff < -(VALID_TICK_WINDOW-1)) {
                            printf("Allowing packet, too late: net_tick %llu, should be >= %llu\n", batch->net_tick, net_tick);
                        }
                    } else {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_DROPPED,
                        };

                        printf("Dropping packet, too early: net_tick %llu, should be >= %llu\n", batch->net_tick, net_tick);

                        new_packet(&peers[peer_index]);
                        APPEND(&peers[peer_index].output_buffer, &response_header);
                        break;
                    }

                    {
                        struct server_batch_header *server_batch = (void *) peers[peer_index].output_buffer.base;
                        server_batch->adjustment = adjustment;
                        server_batch->adjustment_iteration = batch->adjustment_iteration;
                    }

                    for (u16 packet = 0; packet < batch->num_packets; ++packet) {
                        struct client_header *header;
                        POP(&input_buffer, &header);

                        switch (header->type) {
                        case CLIENT_PACKET_UPDATE: {
                            struct client_packet_update *input_update;
                            POP(&input_buffer, &input_update);

                            struct peer *peer = &peers[peer_index];
                            struct update_log_entry entry = {
                                .client_sim_tick = header->sim_tick,
                                .server_net_tick = net_tick,
                                .input_update = *input_update,
                            };
                            CIRCULAR_BUFFER_APPEND(&peer->update_log, entry);
                        } break;
                        default:
                            printf("Received unknown packet type %d\n", header->type);
                        }
                    }
                } break;

                case ENET_EVENT_TYPE_DISCONNECT: {
                    u8 peer_index = *(u8 *) event.peer->data;
                    struct peer *p = &peers[peer_index];
                    printf("%d disconnected.\n", peer_index);

                    {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_PEER_DISCONNECTED,
                        };

                        struct server_packet_peer_disconnected disc = {
                            .peer_index = peer_index,
                        };

                        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                            if (!peers[i].connected || i == peer_index)
                                continue;
                            new_packet(&peers[i]);
                            APPEND(&peers[i].output_buffer, &response_header);
                            APPEND(&peers[i].output_buffer, &disc);
                        }
                    }

                    peer_disconnect(p);
                    --num_peers;
                    free(event.peer->data);
                    event.peer->data = NULL;
                } break;

                case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
                    u8 peer_index = *(u8 *) event.peer->data;
                    struct peer *p = &peers[peer_index];
                    printf("%d disconnected due to timeout.\n", peer_index);

                    {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_PEER_DISCONNECTED,
                        };

                        struct server_packet_peer_disconnected disc = {
                            .peer_index = peer_index,
                        };


                        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                            if (!peers[i].connected || i == peer_index)
                                continue;
                            new_packet(&peers[i]);
                            APPEND(&peers[i].output_buffer, &response_header);
                            APPEND(&peers[i].output_buffer, &disc);
                        }
                    }

                    peer_disconnect(p);
                    --num_peers;
                    free(event.peer->data);
                    event.peer->data = NULL;
                } break;

                case ENET_EVENT_TYPE_NONE:
                    break;
                }
                enet_packet_destroy(event.packet);
            }
        }

        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
            if (!peers[i].connected)
                continue;
            struct peer *peer = &peers[i];
peer_update_log_label:
            if (peer->update_log.used > 0) {
                struct update_log_entry *entry = &peer->update_log.data[peer->update_log.bottom];
                if (entry->client_sim_tick < sim_tick) {
                    printf("%llu, %llu\n", entry->client_sim_tick, sim_tick);
                }

                if (entry->client_sim_tick < sim_tick) {
                    // TODO(anjo): remove goto, I'm lazy
                    //             we somehow ended up with a packet from the past???
                    printf("Something fucky is amiss!\n");
                    CIRCULAR_BUFFER_POP(&peer->update_log);
                    goto peer_update_log_label;
                }

                assert(entry->client_sim_tick >= sim_tick);
                if (entry->client_sim_tick == sim_tick) {
                    move(&map, &peer->player, &entry->input_update.input, dt);
                    peer->update_processed = true;

                    // Send AUTH packet to peer
                    {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_AUTH,
                        };

                        struct server_packet_auth auth = {
                            .sim_tick = entry->client_sim_tick,
                            .player = peer->player,
                        };

                        new_packet(peer);
                        APPEND(&peer->output_buffer, &response_header);
                        APPEND(&peer->output_buffer, &auth);
                    }

                    // Send PEER_AUTH packet to all other peers
                    // TODO(anjo): We are not attaching any adjustment data here
                    {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_PEER_AUTH,
                        };

                        struct server_packet_peer_auth peer_auth = {
                            .sim_tick = entry->client_sim_tick,
                            .player = peer->player,
                            .peer_index = i,
                        };

                        for (u8 j = 0; j < MAX_CLIENTS; ++j) {
                            if (!peers[j].connected || i == j)
                                continue;
                            new_packet(&peers[j]);
                            APPEND(&peers[j].output_buffer, &response_header);
                            APPEND(&peers[j].output_buffer, &peer_auth);
                        }
                    }

                    CIRCULAR_BUFFER_POP(&peer->update_log);
                }
            }
        }

        if (sim_tick % NET_PER_SIM_TICKS == 0) {
            for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                if (!peers[i].connected)
                    continue;
                const size_t size = (intptr_t) peers[i].output_buffer.top - (intptr_t) peers[i].output_buffer.base;
                if (size > sizeof(struct server_batch_header)) {
                    ENetPacket *packet = enet_packet_create(peers[i].output_buffer.base, size, ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(peers[i].enet_peer, 0, packet);
                    peers[i].output_buffer.top = peers[i].output_buffer.base;

                    struct server_batch_header batch = {
                        .num_packets = 0,
                    };
                    APPEND(&peers[i].output_buffer, &batch);
                }
            }
        }

        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
            if (!peers[i].connected)
                continue;
            if (peers[i].update_processed) {
                peers[i].update_processed = false;
                continue;
            }
            struct player *player = &peers[i].player;
            struct input input = {0};
            move(&map, player, &input, dt);
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("server", 10, 10, 20, BLACK);
        if (sim_tick % FPS == 0) {
            fps = 1.0f / ((f32)time_nanoseconds(&frame_delta)/(f32)NANOSECS_PER_SEC);
        }
        if (!isinf(fps))
            DrawText(TextFormat("fps: %.0f", fps), 10, 30, 20, GRAY);
        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
            if (!peers[i].connected)
                continue;
            struct player *player = &peers[i].player;
            {
                const f32 line_len = 30.0f;
                const f32 line_thick = 3.0f;
                DrawLineEx((Vector2) {player->pos.x, player->pos.y}, (Vector2) {player->pos.x + line_len*player->look.x, player->pos.y + line_len*player->look.y}, line_thick, RED);
            }
            DrawCircle(player->pos.x, player->pos.y, 10.0f, RED);
        }
        EndDrawing();

        // End frame
        time_current(&frame_end);
        time_subtract(&frame_delta, &frame_end, &frame_start);
        if (time_less_than(&frame_delta, &frame_desired)) {
            time_subtract(&frame_diff, &frame_desired, &frame_delta);
            nanosleep(&frame_diff, NULL);
        }
        if (sim_tick % NET_PER_SIM_TICKS == 0)
            net_tick++;
        sim_tick++;
    }

    enet_host_destroy(server);
    enet_deinitialize();
    CloseWindow();
    return 0;
}
