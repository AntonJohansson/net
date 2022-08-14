#pragma once

#include "common.h"
#include "update.h"

enum server_packet_type {
    SERVER_PACKET_GREETING,
    SERVER_PACKET_PEER_GREETING,
    SERVER_PACKET_DROPPED,
    SERVER_PACKET_AUTH,
    SERVER_PACKET_PEER_AUTH,
    SERVER_PACKET_PEER_DISCONNECTED,
};

enum client_packet_type {
    CLIENT_PACKET_UPDATE,
};

struct server_header {
    enum server_packet_type type;
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
    u8 peer_index;
};

struct server_packet_peer_greeting {
    f32 initial_x;
    f32 initial_y;
    u8 peer_index;
};

struct server_packet_auth {
    u64 tick;
    f32 x;
    f32 y;
};

struct server_packet_peer_auth {
    u64 tick;
    f32 x;
    f32 y;
    u8 peer_index;
};

struct server_packet_peer_disconnected {
    u8 peer_index;
};

struct client_packet_update {
    struct input input;
};
