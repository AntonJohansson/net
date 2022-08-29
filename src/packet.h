#pragma once

#include "common.h"
#include "update.h"
#include "v2.h"

enum server_packet_type {
    SERVER_PACKET_GREETING,
    SERVER_PACKET_PEER_GREETING,
    SERVER_PACKET_DROPPED,
    SERVER_PACKET_AUTH,
    SERVER_PACKET_PEER_AUTH,
    SERVER_PACKET_PEER_SHOOT,
    SERVER_PACKET_PEER_DISCONNECTED,
};

enum client_packet_type {
    CLIENT_PACKET_UPDATE,
};

struct __attribute((packed)) server_batch_header {
    u16 num_packets;
    i8 adjustment;
    u8 adjustment_iteration;
};

struct server_header {
    enum server_packet_type type;
};

struct client_batch_header {
    u64 net_tick;
    u16 num_packets;
    u8 adjustment_iteration;
};

struct client_header {
    enum client_packet_type type;
    u64 sim_tick;
};

struct server_packet_greeting {
    u64 initial_net_tick;
    v2 initial_pos;
    u8 peer_index;
};

struct server_packet_peer_greeting {
    v2 initial_pos;
    f32 health;
    u8 peer_index;
};

struct server_packet_auth {
    struct player player;
    u64 sim_tick;
};

struct server_packet_peer_auth {
    struct player player;
    u64 sim_tick;
    u8 peer_index;
};

struct server_packet_peer_shoot {
    struct projectile projectile;
    u64 sim_tick;
    u8 peer_index;
};

struct server_packet_peer_disconnected {
    u8 peer_index;
};

struct client_packet_update {
    struct input input;
};
