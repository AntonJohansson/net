#pragma once

#include "v2.h"

enum sound {
    SOUND_SNIPER_FIRE = 0,
    SOUND_NADE_EXPLOSION,
    SOUND_NADE_BEEP,
    SOUND_WEAPON_SWITCH,
    SOUND_STEP,
    SOUND_NADE_DOINK,
    SOUND_PLAYER_SLIDE,
    SOUND_PLAYER_KILL,

    SOUND_COUNT,
};

void audio_init();
void audio_deinit();

void audio_play_spatial_sound(enum sound sound, v2 sound_pos, v2 observer_pos, v2 observer_dir);
