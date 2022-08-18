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
    SERVER_PACKET_PEER_DISCONNECTED,
};

enum client_packet_type {
    CLIENT_PACKET_UPDATE,
};

__attribute__((packed))
struct server_batch_header {
    u16 num_packets;
    i8 adjustment;
    u8 adjustment_iteration;
};

__attribute__((packed))
struct server_header {
    enum server_packet_type type;
};

__attribute__((packed))
struct client_batch_header {
    u16 num_packets;
    u64 net_tick;
    u8 adjustment_iteration;
};

__attribute__((packed))
struct client_header {
    enum client_packet_type type;
    u64 sim_tick;
};

__attribute__((packed))
struct server_packet_greeting {
    u64 initial_net_tick;
    v2 initial_pos;
    u8 peer_index;
};

__attribute__((packed))
struct server_packet_peer_greeting {
    v2 initial_pos;
    u8 peer_index;
};

__attribute__((packed))
struct server_packet_auth {
    u64 sim_tick;
    struct player player;
};

__attribute__((packed))
struct server_packet_peer_auth {
    u64 sim_tick;
    struct player player;
    u8 peer_index;
};

__attribute__((packed))
struct server_packet_peer_disconnected {
    u8 peer_index;
};

__attribute__((packed))
struct client_packet_update {
    struct input input;
};
