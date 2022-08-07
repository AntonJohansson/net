#pragma once

#include "common.h"
#include "update.h"

enum server_packet_type {
    SERVER_PACKET_GREETING,
    SERVER_PACKET_DROPPED,
    SERVER_PACKET_AUTH,
};

enum client_packet_type {
    CLIENT_PACKET_UPDATE,
};

struct server_header {
    enum server_packet_type type;
    u64 tick; // only auth packets
    i8 adjustment;
    u8 adjustment_iteration;
};

struct client_header {
    enum client_packet_type type;
    u64 tick;
    u8 adjustment_iteration;
};

struct server_packet_greeting {
    u64 initial_tick;
    f32 initial_x;
    f32 initial_y;
};

struct server_packet_auth {
    f32 x;
    f32 y;
};

struct client_packet_update {
    struct input input;
};
