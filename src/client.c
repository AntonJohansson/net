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

#define PACKET_LOG_SIZE 2048
#define OUTPUT_BUFFER_SIZE 1024
#define INPUT_BUFFER_LENGTH 16

const u64 initial_server_tick_offset = 0;

bool running = true;

void inthandler(int sig) {
    (void) sig;
    running = false;
}

bool connected = false;

int main() {
    signal(SIGINT, inthandler);

    struct byte_buffer packet_log = {
        .base = malloc(PACKET_LOG_SIZE),
        .size = PACKET_LOG_SIZE,
    };
    packet_log.top = packet_log.base;

    struct byte_buffer output_buffer = {
        .base = malloc(OUTPUT_BUFFER_SIZE),
        .size = OUTPUT_BUFFER_SIZE,
    };
    output_buffer.top = output_buffer.base;

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

    open_window();

    struct timespec frame_start = {0},
                    frame_end   = {0},
                    frame_delta = {0};

    struct timespec frame_desired = {
        .tv_sec = 0,
        .tv_nsec = NANOSECS_PER_SEC / FPS,
    };

    u64 tick = 0;

    const f32 dt = time_nanoseconds(&frame_desired);

    u8 input_count = 0;
    struct input input_buffer[INPUT_BUFFER_LENGTH] = {0};

    struct player player = {0};

    u8 adjustment_ahead = 0;
    u8 adjustment_behind = 0;
    u8 adjustment_iteration = 0;

    while (running) {
        // Begin frame
        time_current(&frame_start);

        if (adjustment_ahead > 0) {
            adjustment_ahead--;
            //if (adjustment_ahead == 0)
            //    adjustment_iteration = 0;
            goto end_frame;
        }

        printf("[ %llu ] --------------------------\n", tick);

        packet_log.top = packet_log.base;

        // Fetch network data
        while (enet_host_service(client, &event, 0) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_RECEIVE: {
                // Copy packets into our own buffer
                // TODO: it would be nice if could just use the buffer that enet fetches it's
                //       packeckts into, we're kinda wasting memory here.
                append(&packet_log, event.packet->data, event.packet->dataLength);
                enet_packet_destroy(event.packet);
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
        }

        // Process packet queue
        u8 *p = packet_log.base;
        while (p < packet_log.top) {
            struct server_header *header = (struct server_header *) p;

            if (header->adjustment != 0 && adjustment_iteration == header->adjustment_iteration) {
                printf("adjustment %d, %d, %d\n", header->adjustment, header->adjustment_iteration, adjustment_iteration);
                if (header->adjustment < 0) {
                    adjustment_ahead = -header->adjustment;
                } else {
                    adjustment_behind = header->adjustment;
                }
                ++adjustment_iteration;
                continue;
            }

            p += sizeof(struct server_header);

            switch (header->type) {
            case SERVER_PACKET_GREETING: {
                struct server_packet_greeting *greeting = (struct server_packet_greeting *) p;
                p += sizeof(struct server_packet_greeting);

                tick = greeting->initial_tick + initial_server_tick_offset;
                player.x = greeting->initial_x;
                player.y = greeting->initial_y;
                connected = true;
            } break;
            case SERVER_PACKET_AUTH: {
                struct server_packet_auth *auth = (struct server_packet_auth *) p;
                p += sizeof(struct server_packet_auth);

                assert(header->tick <= tick);
                u64 diff = tick - header->tick - 1;
                assert(diff < INPUT_BUFFER_LENGTH);

                printf("  Replaying %d inputs from %d+1 to %d-1\n", diff, header->tick, tick);
                printf("    Starting from {%f, %f}\n", auth->x, auth->y);
                printf("    Should match {%f, %f}\n", player.x, player.y);

                // Gets the input for the tick after the tick we recieved auth data for
                struct player old_player = {
                    .x = auth->x,
                    .y = auth->y,
                };
                u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                    struct input *old_input = &input_buffer[old_index];
                    move(&old_player, old_input, dt);
                    printf("  replaying input -> {%f, %f}\n", old_player.x, old_player.y);
                }

                if (!f32_equal(player.x, old_player.x) && !f32_equal(player.y, old_player.y)) {
                    printf("Server disagreed! {%f, %f} vs {%f, %f}, Forcing pos tick %d vs %d!\n",
                           player.x, player.y,
                           old_player.x, old_player.y,
                           header->tick, tick);
                    printf("  -----------------\n");
                    for (u8 i = (input_count + 1) % INPUT_BUFFER_LENGTH; i != input_count; i = (i + 1) % INPUT_BUFFER_LENGTH) {
                        struct input *old_input = &input_buffer[i];
                        printf("  L(%d) R(%d) U(%d) D(%d)",
                               old_input->active[INPUT_MOVE_LEFT] == true,
                               old_input->active[INPUT_MOVE_RIGHT] == true,
                               old_input->active[INPUT_MOVE_UP] == true,
                               old_input->active[INPUT_MOVE_DOWN] == true
                               );
                        if (i == (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH)
                            printf(" <-- start");

                        printf("\n");
                    }
                    printf("  -----------------\n");
                    player.x = old_player.x;
                    player.y = old_player.y;
                }
            } break;
            default:
                printf("Received unknown packet type\n");
            }

        }

        struct input *input = &input_buffer[input_count];
        input_count = (input_count + 1) % INPUT_BUFFER_LENGTH;
        client_handle_input(input);

        if (input->active[INPUT_QUIT])
            running = false;

        if (connected) {
            struct client_header header = {
                .type = CLIENT_PACKET_UPDATE,
                .tick = tick,
                .adjustment_iteration = adjustment_iteration,
            };

            struct client_packet_update update = {
                .input = *input,
            };

            output_buffer.top = output_buffer.base;
            APPEND(&output_buffer, &header);
            APPEND(&output_buffer, &update);

            // Send input
            ENetPacket *packet = enet_packet_create(output_buffer.base, output_buffer.top-output_buffer.base, ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer, 0, packet);

            // Predictive move
            move(&player, input, dt);

            printf("%f, %f\n", player.x, player.y);
        }

        draw("client", &player);

        // End frame
end_frame:
        if (adjustment_behind == 0) {
            // Only sleep remaining frame time if we aren't fast forwarding
            time_current(&frame_end);
            time_subtract(&frame_delta, &frame_end, &frame_start);
            if (time_less_than(&frame_delta, &frame_desired)) {
                time_subtract(&frame_delta, &frame_desired, &frame_delta);
                nanosleep(&frame_delta, NULL);
            }
        }
        if (adjustment_behind > 0) {
            adjustment_behind--;
            //if (adjustment_behind == 0)
            //    adjustment_iteration = 0;
        }
        if (adjustment_ahead == 0)
            tick++;
    }

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
        }
    }

    // Drop connection, since disconnection didn't successed
    if (!disconnected) {
        enet_peer_reset(peer);
    }

    enet_host_destroy(client);
    enet_deinitialize();
    close_window();
    return 0;
}
