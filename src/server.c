#define ENET_IMPLEMENTATION
#include "enet.h"
#include "packet.h"
#include "draw.h"
#include "common.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>

#define FPS 60

#define MAX_CLIENTS 32

#define PACKET_LOG_SIZE 2048
#define OUTPUT_BUFFER_SIZE 1024
#define INPUT_BUFFER_LENGTH 16
#define UPDATE_LOG_BUFFER_SIZE 512

bool running = true;

void inthandler(int sig) {
    (void) sig;
    running = false;
}

struct update_log_entry {
    ENetPeer *peer;
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

int main() {
    signal(SIGINT, inthandler);

    struct player player = {
        .x = 400.0f,
        .y = 300.0f,
    };

    struct byte_buffer output_buffer = {
        .base = malloc(OUTPUT_BUFFER_SIZE),
        .size = OUTPUT_BUFFER_SIZE,
    };
    output_buffer.top = output_buffer.base;

    struct update_log_buffer update_log = {0};

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

    printf("Started a server...\n");

    open_window();

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

    while (running) {
        time_current(&frame_start);

        // Handle network
        while (enet_host_service(server, &event, 0) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                printf("A new client connected from %x:%u.\n",  event.peer->address.host, event.peer->address.port);
                struct server_header header = {
                    .type = SERVER_PACKET_GREETING,
                };

                struct server_packet_greeting greeting = {
                    .initial_tick = tick,
                    .initial_x = player.x,
                    .initial_y = player.y,
                };

                output_buffer.top = output_buffer.base;
                APPEND(&output_buffer, &header);
                APPEND(&output_buffer, &greeting);

                ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(event.peer, 0, packet);

                break;

            case ENET_EVENT_TYPE_RECEIVE: {
                char *p = event.packet->data;
                struct client_header *header = (struct client_header *) p;
                p += sizeof(struct client_header);

                switch (header->type) {
                case CLIENT_PACKET_UPDATE: {
                    i8 adjustment = 0;
                    i64 diff = (i64) tick + (2-1) - (i64) header->tick;
                    if (diff < INT8_MIN || diff > INT8_MAX) {
                        printf("tick diff outside range of adjustment variable!\n");
                        // TODO: what do?
                        break;
                    }
                    if (diff < -(2-1) || diff > 0) {
                        // Need adjustment
                        adjustment = (i8) diff;
                    }

                    if (header->tick >= tick) {
                        struct client_packet_update *input_update = (struct client_packet_update *) p;

                        if (diff < -(2-1)) {
                            printf("Allowing packet, too late: tick %llu, should be >= %llu\n", header->tick, tick);
                        } else {
                            printf("Allowing packet, tick %llu, %llu\n", header->tick, tick);
                        }

                        struct update_log_entry entry = {
                            .peer = event.peer,
                            .client_tick = header->tick,
                            .server_tick = tick,
                            .adjustment = adjustment,
                            .adjustment_iteration = header->adjustment_iteration,
                            .input_update = *input_update,
                        };
                        CIRCULAR_BUFFER_APPEND(&update_log, entry);
                    } else {
                        struct server_header response_header = {
                            .type = SERVER_PACKET_DROPPED,
                            .tick = tick, // not used by client
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
                enet_packet_destroy(event.packet);
            } break;

            case ENET_EVENT_TYPE_DISCONNECT:
                printf("%s disconnected.\n", event.peer->data);
                event.peer->data = NULL;
                break;

            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                printf("%s disconnected due to timeout.\n", event.peer->data);
                event.peer->data = NULL;
                break;

            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }

        if (update_log.used > 0) {
            struct update_log_entry entry = update_log.data[update_log.bottom];
            if (entry.client_tick == tick) {
                struct server_header response_header = {
                    .type = SERVER_PACKET_AUTH,
                    .tick = entry.client_tick,
                    .adjustment = entry.adjustment,
                    .adjustment_iteration = entry.adjustment_iteration,
                };

                //printf("Calculating auth data for client tick %llu/server tick %llu at tick %llu\n", entry.client_tick, entry.server_tick, tick);

                // Currently applying all packets immediately
                // CONTHERE: Since we're applying it directly, we'll send auth data for the wrong
                // tick, this will cause forced applying of auth data for ticks where the input
                // changes.
                //
                // We need to buffer the input data and apply it on the correct frame!
                //
                move(&player, &entry.input_update.input, dt);

                struct server_packet_auth auth = {
                    .x = player.x,
                    .y = player.y,
                };

                output_buffer.top = output_buffer.base;
                APPEND(&output_buffer, &response_header);
                APPEND(&output_buffer, &auth);

                ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(entry.peer, 0, packet);

                CIRCULAR_BUFFER_POP(&update_log);
            }
        }

        draw("server", &player);

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
    close_window();
    return 0;
}
