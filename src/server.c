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

struct server_peer {
    PlayerId id;
    struct update_log_buffer update_log;
    struct byte_buffer output_buffer;
    ENetPeer *enet_peer;
};

static inline void new_packet(struct server_peer *p) {
    struct server_batch_header *batch = (void *) p->output_buffer.base;
    assert(batch->num_packets < UINT16_MAX);
    ++batch->num_packets;
}

static inline void randomize_player_spawn(struct random_series_pcg *random, struct map m, struct player *p) {
retry:;

    f32 x = m.width  * random_next_unilateral(random);
    f32 y = m.height * random_next_unilateral(random);
    if (map_at(&m, (v2){x,y}) == TILE_STONE)
        goto retry;
    p->pos.x = x;
    p->pos.y = y;
}

struct respawn_list_item {
    PlayerId id;
    f32 time_left;
};

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

    HashMap(struct server_peer, MAX_CLIENTS) peer_map = {0};

    struct random_series_pcg random = random_seed_pcg(0x9053, 0x9005);

#if defined(DRAW)
    InitWindow(WIDTH, HEIGHT, "floating");
    HideCursor();
#endif

    List(struct respawn_list_item, MAX_CLIENTS) respawn_list = {0};

    u64 total_delta = 0;

    while (running) {
        const u64 total_frame_start = time_current();
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

                    const u64 id = player_id();

                    event.peer->data = malloc(sizeof(PlayerId));
                    *(PlayerId *)event.peer->data = id;

                    struct server_peer *peer = NULL;
                    HashMapInsert(peer_map, id, peer);
                    peer->enet_peer = event.peer;
                    peer->output_buffer = byte_buffer_alloc(OUTPUT_BUFFER_SIZE);

                    struct player *p = NULL;
                    HashMapInsert(game.player_map, id, p);

                    struct respawn_list_item item = {id, 0.1f};
                    ListInsert(respawn_list, item);

                    struct server_batch_header batch = {
                        .num_packets = 0,
                    };
                    APPEND(&peer->output_buffer, &batch);

                    // Send greeting for peer
                    {
                        struct server_header header = {
                            .type = SERVER_PACKET_GREETING,
                        };

                        struct server_packet_greeting greeting = {
                            .initial_net_tick = frame.network_tick,
                            .id = id,
                        };

                        new_packet(peer);
                        APPEND(&peer->output_buffer, &header);
                        APPEND(&peer->output_buffer, &greeting);
                    }

                    // Send greeting to all other peers
                    {
                        struct server_header header = {
                            .type = SERVER_PACKET_PEER_GREETING,
                        };

                        struct server_packet_peer_greeting greeting = {
                            .id = id,
                        };

                        HashMapForEach(peer_map, struct server_peer, other_peer) {
                            if (!HashMapExists(peer_map, other_peer) || peer == other_peer)
                                continue;
                            new_packet(other_peer);
                            APPEND(&other_peer->output_buffer, &header);
                            APPEND(&other_peer->output_buffer, &greeting);
                        }
                    }

                    // Send greeting to this peer about all other peers already connected
                    {
                        struct server_header header = {
                            .type = SERVER_PACKET_PEER_GREETING,
                        };

                        HashMapForEach(peer_map, struct server_peer, other_peer) {
                            if (!HashMapExists(peer_map, other_peer) || peer == other_peer)
                                continue;
                            struct server_packet_peer_greeting greeting = {
                                .id = other_peer->id,
                            };

                            new_packet(peer);
                            APPEND(&peer->output_buffer, &header);
                            APPEND(&peer->output_buffer, &greeting);
                        }
                    }
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    struct byte_buffer input_buffer = byte_buffer_init(event.packet->data, event.packet->dataLength);
                    struct client_batch_header *batch;
                    POP(&input_buffer, &batch);

                    const PlayerId id = *(PlayerId *)event.peer->data;

                    struct server_peer *peer = NULL;
                    HashMapLookup(peer_map, id, peer);

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
                        struct server_batch_header *server_batch = (void *) peer->output_buffer.base;
                        server_batch->adjustment = adjustment;
                        server_batch->adjustment_iteration = batch->adjustment_iteration;
                    }

                    if (tick >= frame.simulation_tick) {
                        if (diff < -(VALID_TICK_WINDOW-1)) {
                            printf("Allowing packet, too late: net_tick %lu, should be >= %lu\n", batch->net_tick, frame.network_tick);
                            printf("adjustment (%u): %d\n", batch->adjustment_iteration, adjustment);
                        }
                    } else {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_DROPPED,
                        };

                        printf("Dropping packet, too early: net_tick %lu, should be >= %lu\n", batch->net_tick, frame.network_tick);
                        printf("adjustment (%u): %d\n", batch->adjustment_iteration, adjustment);

                        new_packet(peer);
                        APPEND(&peer->output_buffer, &response_header);
                        break;
                    }

                    for (u16 packet = 0; packet < batch->num_packets; ++packet) {
                        struct client_header *header;
                        POP(&input_buffer, &header);

                        switch (header->type) {
                        case CLIENT_PACKET_UPDATE: {
                            struct client_packet_update *input_update;
                            POP(&input_buffer, &input_update);

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

                case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                case ENET_EVENT_TYPE_DISCONNECT: {
                    PlayerId id = *(PlayerId *) event.peer->data;

                    struct server_peer *peer = NULL;
                    HashMapLookup(peer_map, id, peer);

                    printf("%d disconnected (%s).\n", id, (event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT) ? "timeout" : "quit");

                    {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_PEER_DISCONNECTED,
                        };

                        struct server_packet_peer_disconnected disc = {
                            .player_id = id,
                        };

                        HashMapForEach(peer_map, struct server_peer, other_peer) {
                            if (!HashMapExists(peer_map, other_peer) || other_peer == peer)
                                continue;
                            new_packet(other_peer);
                            APPEND(&other_peer->output_buffer, &response_header);
                            APPEND(&other_peer->output_buffer, &disc);
                        }
                    }

                    byte_buffer_free(&peer->output_buffer);
                    HashMapRemove(game.player_map, id);
                    HashMapRemove(peer_map, id);
                    free(event.peer->data);
                    event.peer->data = NULL;
                } break;

                case ENET_EVENT_TYPE_NONE:
                    break;
                }
                enet_packet_destroy(event.packet);
            }
        }

        HashMapForEach(peer_map, struct server_peer, peer) {
            if (!HashMapExists(peer_map, peer))
                continue;

            struct player *player = NULL;
            HashMapLookup(game.player_map, peer->id, player);

            while (peer->update_log.used > 0) {
                struct update_log_entry *entry = &peer->update_log.data[peer->update_log.bottom];
                if (entry->client_sim_tick > frame.simulation_tick)
                    break;

                struct input input = entry->input_update.input;
                update_player(&game, player, &input, frame.dt);
                collect_and_resolve_static_collisions(&game);

                // We don't care about sound
                ListClear(game.sound_list);

                // Send AUTH packet to peer
                {
                    struct server_header response_header = {
                        .type = SERVER_PACKET_AUTH,
                    };

                    struct server_packet_auth auth = {
                        .sim_tick = entry->client_sim_tick,
                        .player = *player,
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
                        .player = *player,
                    };

                    HashMapForEach(peer_map, struct server_peer, other_peer) {
                        if (!HashMapExists(peer_map, other_peer) || other_peer == peer)
                            continue;
                        new_packet(other_peer);
                        APPEND(&other_peer->output_buffer, &response_header);
                        APPEND(&other_peer->output_buffer, &peer_auth);
                    }
                }

                CIRCULAR_BUFFER_POP(&peer->update_log);
            }

            //struct collision_result results[16] = {0};
            //u32 num_results = 0;
            //collect_dynamic_collisions(&game, results, &num_results, 16);
            //resolve_dynamic_collisions(&game, results, num_results);
        }

        ForEachList(respawn_list, struct respawn_list_item, item) {
            item->time_left -= frame.dt;

            if (item->time_left <= 0.0f) {
                struct player *p = NULL;
                HashMapLookup(game.player_map, item->id, p);

                randomize_player_spawn(&random, game.map, p);
                p->weapons[0] = PLAYER_WEAPON_SNIPER;
                p->weapons[1] = PLAYER_WEAPON_NADE;
                p->hue = 20.0f + 80.0f*random_next_unilateral(&random);
                p->health = 100.0f;

                struct server_peer *peer = NULL;
                HashMapLookup(peer_map, item->id, peer);

                {
                    struct server_header response_header = {
                        .type = SERVER_PACKET_PLAYER_SPAWN,
                    };

                    struct server_packet_player_spawn spawn = {
                        .player = *p,
                    };

                    new_packet(peer);
                    APPEND(&peer->output_buffer, &response_header);
                    APPEND(&peer->output_buffer, &spawn);
                }

                ListTagRemovePtr(respawn_list, item);
            }
        }
        ListRemoveTaggedItems(respawn_list);


        ForEachList(game.new_nade_list, struct nade_projectile, nade) {
            struct player *p = NULL;
            HashMapLookup(game.player_map, nade->player_id_from, p);

            // Loop over all connected peers and send kill packet
            HashMapForEach(peer_map, struct server_peer, other_peer) {
                if (!HashMapExists(peer_map, other_peer) || other_peer->id == p->id)
                    continue;

                {
                    struct server_header header = {
                        .type = SERVER_PACKET_NADE,
                    };

                    struct server_packet_nade nade_packet = {
                        .nade = *nade,
                    };

                    new_packet(other_peer);
                    APPEND(&other_peer->output_buffer, &header);
                    APPEND(&other_peer->output_buffer, &nade_packet);
                }
            }
        }

        ForEachList(game.new_hitscan_list, struct hitscan_projectile, hitscan) {
            struct player *p = NULL;
            HashMapLookup(game.player_map, hitscan->player_id_from, p);

            // Loop over all connected peers and send kill packet
            HashMapForEach(peer_map, struct server_peer, other_peer) {
                if (!HashMapExists(peer_map, other_peer) || other_peer->id == p->id)
                    continue;

                {
                    struct server_header header = {
                        .type = SERVER_PACKET_HITSCAN,
                    };

                    struct server_packet_hitscan hitscan_packet = {
                        .hitscan = *hitscan,
                    };

                    new_packet(other_peer);
                    APPEND(&other_peer->output_buffer, &header);
                    APPEND(&other_peer->output_buffer, &hitscan_packet);
                }
            }
        }
        ListClear(game.new_nade_list);
        ListClear(game.new_hitscan_list);

        // Now it's time for per-frame updates
        update_projectiles(&game, frame.dt);

        // Apply damage
        ForEachList(game.damage_list, struct damage_entry, d) {
            struct player *p = NULL;
            HashMapLookup(game.player_map, d->player_id, p);
            p->health -= d->damage;

            if (p->health <= 0.0f) {
                struct respawn_list_item item = {p->id, 1.0f};
                ListInsert(respawn_list, item);

                // Loop over all connected peers and send kill packet
                HashMapForEach(peer_map, struct server_peer, other_peer) {
                    if (!HashMapExists(peer_map, other_peer))
                        continue;

                    {
                        struct server_header header = {
                            .type = SERVER_PACKET_PLAYER_KILL,
                        };

                        struct server_packet_player_kill kill = {
                            .player_id = p->id,
                        };

                        new_packet(other_peer);
                        APPEND(&other_peer->output_buffer, &header);
                        APPEND(&other_peer->output_buffer, &kill);
                    }
                }
            }
        }
        ListClear(game.damage_list);

        // If we're on a network tick, then send batch
        if (frame.simulation_tick % NET_PER_SIM_TICKS == 0) {
            HashMapForEach(peer_map, struct server_peer, peer) {
                if (!HashMapExists(peer_map, peer))
                    continue;
                const size_t size = (intptr_t) peer->output_buffer.top - (intptr_t) peer->output_buffer.base;
                if (size > sizeof(struct server_batch_header)) {
                    ENetPacket *packet = enet_packet_create(peer->output_buffer.base, size, ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(peer->enet_peer, 0, packet);
                    peer->output_buffer.top = peer->output_buffer.base;

                    struct server_batch_header batch = {
                        .num_packets = 0,
                    };
                    APPEND(&peer->output_buffer, &batch);
                }
            }
        }

#if defined(DRAW)
        if (IsKeyDown(KEY_Q))
            running = false;
        BeginDrawing();
        ClearBackground(RAYWHITE);
        //draw_game(&game, t);

        draw_all_debug_v2s((struct camera) {0});

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
                printf("fps: %.0f (%.0f)\n", fps, 1000000000.0f/((f32)total_delta));
        }
#endif

        // End frame
        const u64 frame_end = time_current();
        frame.delta = frame_end - frame_start;
        if (frame.delta < frame.desired_delta) {
            time_nanosleep(frame.desired_delta - frame.delta);
        }
        const u64 total_frame_end = time_current();
        total_delta = total_frame_end - total_frame_start;
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
