#define ENET_IMPLEMENTATION
#include "enet.h"
#include "packet.h"
#include "common.h"
#include "random.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#if defined(DRAW)
#include "draw.h"
#include <raylib.h>

#define WIDTH 800
#define HEIGHT 600
#endif

#define PACKET_LOG_SIZE 2048
#define OUTPUT_BUFFER_SIZE 32000
#define INPUT_BUFFER_LENGTH 16
#define UPDATE_LOG_BUFFER_SIZE 512
#define VALID_TICK_WINDOW 5

bool running = true;

//
// TODO(anjo): We should attach adjustment info to the earliest possible packet that returns to the
//             client. Currently we attach the adjustment info whenever we process the update packet.
//             This should lead to delays in getting the client synced, but shouldn't cause more problems
//             than that.
//

struct frame {
    u64 desired_delta;
    u64 delta;
    u64 simulation_tick;
    u64 network_tick;
    f32 dt;
};

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
    struct player *player;
    struct update_log_buffer update_log;
    struct byte_buffer output_buffer;
    ENetPeer *enet_peer;
};

static inline void peer_disconnect(struct peer *p) {
    p->player->occupied = false;
    memset(p, 0, sizeof(struct peer));
    byte_buffer_free(&p->output_buffer);
}

static inline void new_packet(struct peer *p) {
    struct server_batch_header *batch = (void *) p->output_buffer.base;
    assert(batch->num_packets < UINT16_MAX);
    ++batch->num_packets;
}

static inline void randomize_player_spawn(struct random_series_pcg random, struct map m, struct player *p) {

retry:;

    f32 x = m.width  * random_next_unilateral(&random);
    f32 y = m.height * random_next_unilateral(&random);
    if (map_at(&m, (v2){x,y}) == TILE_STONE)
        goto retry;
    p->pos.x = x;
    p->pos.y = y;
}

int main() {
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

    f32 t = 0.0f;
    f32 fps = 0.0f;

    struct frame frame = {
        .desired_delta = NANOSECONDS(1) / (f32) FPS,
        .dt = 1.0f / (f32) FPS,
    };

    struct game game = {
        .map = map,
    };

    struct peer peers[MAX_CLIENTS] = {0};
    u8 num_peers = 0;

    struct random_series_pcg random = random_seed_pcg(0x9053, 0x9005);

#if defined(DRAW)
    InitWindow(WIDTH, HEIGHT, "floating");
    HideCursor();
#endif

    while (running) {
        const u64 frame_start = time_current();

        // Handle network
        if (frame.simulation_tick % NET_PER_SIM_TICKS == 0) {
            while (enet_host_service(server, &event, 0) > 0) {
                switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    i8 ip[64] = {0};
                    if (enet_address_get_host_ip_new(&event.peer->address, (char *) ip, ARRLEN(ip)) == 0) {
                        printf("A new client connected from %s:%u.\n", ip, event.peer->address.port);
                    } else {
                        printf("A new client connected from ????:%u.\n", event.peer->address.port);
                    }

                    assert(num_peers < MAX_CLIENTS);
                    u8 peer_index = 0;
                    for (; peer_index < MAX_CLIENTS; ++peer_index)
                        if (!peers[peer_index].connected)
                            break;
                    ++num_peers;
                    event.peer->data = malloc(sizeof(u8));
                    *(u8 *)event.peer->data = peer_index;

                    peers[peer_index].connected = true;

                    const u64 id = player_id();
                    struct player *p = allocate_player(&game, id);
                    //randomize_player_spawn(random, game.map, p);
                    p->pos = (v2){2,2};
                    p->hue = 20.0f;
                    peers[peer_index].player = p;

                    peers[peer_index].enet_peer = event.peer;
                    peers[peer_index].output_buffer = byte_buffer_alloc(OUTPUT_BUFFER_SIZE);

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
                            .initial_net_tick = frame.network_tick,
                            .id = id,
                            .initial_pos = peers[peer_index].player->pos,
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
                            .initial_pos = peers[peer_index].player->pos,
                            .id = id,
                            .health = peers[peer_index].player->health,
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
                                .initial_pos = peers[i].player->pos,
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
                    struct byte_buffer input_buffer = byte_buffer_init(event.packet->data, event.packet->dataLength);
                    struct client_batch_header *batch;
                    POP(&input_buffer, &batch);

                    const u8 peer_index = *(u8 *)event.peer->data;
                    assert(peer_index >= 0 && peer_index < MAX_CLIENTS);
                    assert(peers[peer_index].connected);

                    assert(batch->num_packets > 0);
                    i64 tick = (i64) ((struct client_header *) input_buffer.top)->sim_tick;

                    i8 adjustment = 0;
                    i64 diff = (i64) frame.simulation_tick + (VALID_TICK_WINDOW-1) - tick;
                    if (diff < INT8_MIN || diff > INT8_MAX) {
                        printf("net_tick diff outside range of adjustment variable!\n");
                        // TODO(anjo): what do?
                        break;
                    }
                    if (diff < -(VALID_TICK_WINDOW-1) || diff > 0) {
                        // Need adjustment
                        adjustment = (i8) diff;
                    }

                    {
                        struct server_batch_header *server_batch = (void *) peers[peer_index].output_buffer.base;
                        server_batch->adjustment = adjustment;
                        server_batch->adjustment_iteration = batch->adjustment_iteration;
                    }

                    if (tick >= frame.simulation_tick) {
                        if (diff < -(VALID_TICK_WINDOW-1)) {
                            printf("Allowing packet, too late: net_tick %lu, should be >= %lu\n", batch->net_tick, frame.network_tick);
                        }
                    } else {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_DROPPED,
                        };

                        printf("Dropping packet, too early: net_tick %lu, should be >= %lu\n", batch->net_tick, frame.network_tick);

                        new_packet(&peers[peer_index]);
                        APPEND(&peers[peer_index].output_buffer, &response_header);
                        break;
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
                                .server_net_tick = frame.network_tick,
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

            while (peer->update_log.used > 0) {
                struct update_log_entry *entry = &peer->update_log.data[peer->update_log.bottom];
                if (entry->client_sim_tick > frame.simulation_tick)
                    break;

                move(&game, peer->player, &entry->input_update.input, frame.dt, false);
                collect_and_resolve_static_collisions(&game);

                // Send AUTH packet to peer
                {
                    struct server_header response_header = {
                        .type = SERVER_PACKET_AUTH,
                    };

                    struct server_packet_auth auth = {
                        .sim_tick = entry->client_sim_tick,
                        .player = *peer->player,
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
                        .player = *peer->player,
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

            //struct collision_result results[16] = {0};
            //u32 num_results = 0;
            //collect_dynamic_collisions(&game, results, &num_results, 16);
            //resolve_dynamic_collisions(&game, results, num_results);
        }

        if (frame.simulation_tick % NET_PER_SIM_TICKS == 0) {
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

#if defined(DRAW)
        if (IsKeyDown(KEY_Q))
            running = false;
        BeginDrawing();
        ClearBackground(RAYWHITE);
        //draw_game(&game, t);

        DrawText("server", 10, 10, 20, BLACK);
        if (frame.simulation_tick % FPS == 0) {
            fps = 1.0f / ((f32)frame.delta/(f32)NANOSECONDS(1));
        }
        if (!isinf(fps))
            DrawText(TextFormat("fps: %.0f", fps), 10, 30, 20, GRAY);

        EndDrawing();
#else
        if (frame.simulation_tick % FPS == 0) {
            fps = 1.0f / ((f32)frame.delta/(f32)NANOSECONDS(1));
            if (!isinf(fps))
                printf("fps: %.0f\n", fps);
        }
#endif

        // End frame
        const u64 frame_end = time_current();
        frame.delta = frame_end - frame_start;
        if (frame.delta < frame.desired_delta) {
            time_nanosleep(frame.desired_delta - frame.delta);
        }
        if (frame.simulation_tick % NET_PER_SIM_TICKS == 0)
            ++frame.network_tick;
        ++frame.simulation_tick;
        t += frame.dt;
    }

    enet_host_destroy(server);
    enet_deinitialize();
#if defined(DRAW)
    CloseWindow();
#endif
    return 0;
}
