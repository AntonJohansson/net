#pragma once

#include "common.h"
#include "v2.h"
#include "game.h"

enum server_packet_type {
    SERVER_PACKET_GREETING,
    SERVER_PACKET_PEER_GREETING,
    SERVER_PACKET_DROPPED,
    SERVER_PACKET_AUTH,
    SERVER_PACKET_PEER_AUTH,
    SERVER_PACKET_PEER_DISCONNECTED,
    SERVER_PACKET_PLAYER_KILL,
    SERVER_PACKET_PLAYER_SPAWN,
    SERVER_PACKET_HITSCAN,
    SERVER_PACKET_NADE,
};

enum client_packet_type {
    CLIENT_PACKET_UPDATE,
};

struct __attribute__((packed)) server_batch_header {
    u16 num_packets;
    i8 adjustment;
    u8 adjustment_iteration;
    u64 avg_drift;
};

struct __attribute__((packed)) server_header {
    enum server_packet_type type;
};

struct __attribute__((packed)) client_batch_header {
    u64 net_tick;
    u16 num_packets;
    u8 adjustment_iteration;
    u64 avg_total_frame_time;
};

struct __attribute__((packed)) client_header {
    enum client_packet_type type;
    u64 sim_tick;
};

struct __attribute__((packed)) server_packet_greeting {
    u64 initial_net_tick;
    u64 id;
};

struct __attribute__((packed)) server_packet_peer_greeting {
    u64 id;
    u8 peer_index;
};

struct __attribute__((packed)) server_packet_auth {
    struct player player;
    u64 sim_tick;
};

struct __attribute__((packed)) server_packet_peer_auth {
    struct player player;
    u64 sim_tick;
    u8 peer_index;
};

struct __attribute__((packed)) server_packet_peer_disconnected {
    PlayerId player_id;
};

struct __attribute__((packed)) server_packet_player_spawn {
    struct player player;
};

struct __attribute__((packed)) server_packet_player_kill {
    u64 player_id;
};

struct __attribute__((packed)) server_packet_hitscan {
    struct hitscan_projectile hitscan;
};

struct __attribute__((packed)) server_packet_nade {
    struct nade_projectile nade;
};

struct __attribute__((packed)) client_packet_update {
    struct input input;
};
