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

#define FPS 60

#define MAX_CLIENTS 4

#define PACKET_LOG_SIZE 2048
#define OUTPUT_BUFFER_SIZE 1024
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
    u64 client_tick;
    u64 server_tick;
    i8 adjustment;
    u8 adjustment_iteration;
    struct client_packet_update input_update;
};

struct update_log_buffer {
    struct update_log_entry data[UPDATE_LOG_BUFFER_SIZE];
    u64 bottom;
    u64 used;
};

struct peer {
    bool connected;
    struct player player;
    struct update_log_buffer update_log;
    ENetPeer *enet_peer;
};

static inline void peer_disconnect(struct peer *p) {
    memset(p, 0, sizeof(struct peer));
}

int main() {
    signal(SIGINT, inthandler);

    struct byte_buffer output_buffer = {
        .base = malloc(OUTPUT_BUFFER_SIZE),
        .size = OUTPUT_BUFFER_SIZE,
    };
    output_buffer.top = output_buffer.base;

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
                    frame_delta = {0};

    struct timespec frame_desired = {
        .tv_sec = 0,
        .tv_nsec = NANOSECS_PER_SEC / FPS,
    };

    u64 tick = 0;
    const f32 dt = time_nanoseconds(&frame_desired);

    struct peer peers[MAX_CLIENTS] = {0};
    u8 num_peers = 0;

    InitWindow(WIDTH, HEIGHT, "server");

    while (running) {
        time_current(&frame_start);

        // Handle network
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
                peers[peer_index].player.x = 400.0f;
                peers[peer_index].player.y = 300.0f;
                peers[peer_index].enet_peer = event.peer;

                // Send greeting for peer
                {
                    struct server_header header = {
                        .type = SERVER_PACKET_GREETING,
                    };

                    struct server_packet_greeting greeting = {
                        .initial_tick = tick,
                        .initial_x = peers[peer_index].player.x,
                        .initial_y = peers[peer_index].player.y,
                        .peer_index = peer_index,
                    };

                    output_buffer.top = output_buffer.base;
                    APPEND(&output_buffer, &header);
                    APPEND(&output_buffer, &greeting);

                    ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, packet);
                }

                // Send greeting to all other peers
                {
                    struct server_header header = {
                        .type = SERVER_PACKET_PEER_GREETING,
                    };

                    struct server_packet_peer_greeting greeting = {
                        .initial_x = peers[peer_index].player.x,
                        .initial_y = peers[peer_index].player.y,
                        .peer_index = peer_index,
                    };

                    output_buffer.top = output_buffer.base;
                    APPEND(&output_buffer, &header);
                    APPEND(&output_buffer, &greeting);

                    ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);
                    for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                        if (!peers[i].connected || i == peer_index)
                            continue;
                        enet_peer_send(peers[i].enet_peer, 0, packet);
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
                            .initial_x = peers[i].player.x,
                            .initial_y = peers[i].player.y,
                            .peer_index = i,
                        };

                        output_buffer.top = output_buffer.base;
                        APPEND(&output_buffer, &header);
                        APPEND(&output_buffer, &greeting);

                        ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(event.peer, 0, packet);
                    }
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                char *p = event.packet->data;
                struct client_header *header = (struct client_header *) p;
                p += sizeof(struct client_header);

                switch (header->type) {
                case CLIENT_PACKET_UPDATE: {
                    i8 adjustment = 0;
                    i64 diff = (i64) tick + (VALID_TICK_WINDOW-1) - (i64) header->tick;
                    if (diff < INT8_MIN || diff > INT8_MAX) {
                        printf("tick diff outside range of adjustment variable!\n");
                        // TODO(anjo): what do?
                        break;
                    }
                    if (diff < -(VALID_TICK_WINDOW-1) || diff > 0) {
                        // Need adjustment
                        adjustment = (i8) diff;
                    }

                    if (header->tick >= tick) {
                        struct client_packet_update *input_update = (struct client_packet_update *) p;

                        if (diff < -(VALID_TICK_WINDOW-1)) {
                            printf("Allowing packet, too late: tick %llu, should be >= %llu\n", header->tick, tick);
                        }

                        const u8 peer_index = *(u8 *)event.peer->data;
                        assert(peer_index >= 0 && peer_index < MAX_CLIENTS);
                        struct peer *peer = &peers[peer_index];
                        struct update_log_entry entry = {
                            .client_tick = header->tick,
                            .server_tick = tick,
                            .adjustment = adjustment,
                            .adjustment_iteration = header->adjustment_iteration,
                            .input_update = *input_update,
                        };
                        CIRCULAR_BUFFER_APPEND(&peer->update_log, entry);
                    } else {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_DROPPED,
                            .adjustment = adjustment,
                            .adjustment_iteration = header->adjustment_iteration,
                        };

                        printf("Dropping packet, too early: tick %llu, should be >= %llu\n", header->tick, tick);

                        output_buffer.top = output_buffer.base;
                        APPEND(&output_buffer, &response_header);

                        ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(event.peer, 0, packet);
                    }

                } break;
                default:
                    printf("Received unknown packet type\n");
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

                    output_buffer.top = output_buffer.base;
                    APPEND(&output_buffer, &response_header);
                    APPEND(&output_buffer, &disc);
                    ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);

                    for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                        if (!peers[i].connected || i == peer_index)
                            continue;
                        enet_peer_send(peers[i].enet_peer, 0, packet);
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

                    output_buffer.top = output_buffer.base;
                    APPEND(&output_buffer, &response_header);
                    APPEND(&output_buffer, &disc);
                    ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);

                    for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                        if (!peers[i].connected || i == peer_index)
                            continue;
                        enet_peer_send(peers[i].enet_peer, 0, packet);
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

        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
            if (!peers[i].connected)
                continue;
            struct peer *peer = &peers[i];
            if (peer->update_log.used > 0) {
                struct update_log_entry *entry = &peer->update_log.data[peer->update_log.bottom];
                if (entry->client_tick <= tick) {
                    move(&peer->player, &entry->input_update.input, dt);

                    // Send AUTH packet to peer
                    {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_AUTH,
                            .adjustment = entry->adjustment,
                            .adjustment_iteration = entry->adjustment_iteration,
                        };

                        struct server_packet_auth auth = {
                            .tick = entry->client_tick,
                            .x = peer->player.x,
                            .y = peer->player.y,
                        };

                        output_buffer.top = output_buffer.base;
                        APPEND(&output_buffer, &response_header);
                        APPEND(&output_buffer, &auth);
                        ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(peer->enet_peer, 0, packet);
                    }

                    // Send PEER_AUTH packet to all other peers
                    // TODO(anjo): We are not attaching any adjustment data here
                    {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_PEER_AUTH,
                        };

                        struct server_packet_peer_auth peer_auth = {
                            .tick = entry->client_tick,
                            .x = peer->player.x,
                            .y = peer->player.y,
                            .peer_index = i,
                        };

                        output_buffer.top = output_buffer.base;
                        APPEND(&output_buffer, &response_header);
                        APPEND(&output_buffer, &peer_auth);
                        ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);

                        for (u8 j = 0; j < MAX_CLIENTS; ++j) {
                            if (!peers[j].connected || i == j)
                                continue;
                            enet_peer_send(peers[j].enet_peer, 0, packet);
                        }
                    }

                    CIRCULAR_BUFFER_POP(&peer->update_log);
                }
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("server", 10, 10, 20, BLACK);
        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
            if (!peers[i].connected)
                continue;
            DrawCircle(peers[i].player.x, peers[i].player.y, 10.0f, RED);
        }
        EndDrawing();

        // End frame
        time_current(&frame_end);
        time_subtract(&frame_delta, &frame_end, &frame_start);
        if (time_less_than(&frame_delta, &frame_desired)) {
            time_subtract(&frame_delta, &frame_desired, &frame_delta);
            nanosleep(&frame_delta, NULL);
        }
        tick++;
    }

    enet_host_destroy(server);
    enet_deinitialize();
    CloseWindow();
    return 0;
}
