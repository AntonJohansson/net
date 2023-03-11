#include "common.h"
#include "audio.h"
#include <raylib.h>

static Sound sound_map[SOUND_COUNT] = {0};

static f32 sound_base_volume[SOUND_COUNT] = {
    [SOUND_SNIPER_FIRE]         = 1.0f,
    [SOUND_NADE_EXPLOSION]      = 1.0f,
    [SOUND_STEP]                = 0.25f,
};

void audio_init() {
    InitAudioDevice();

    sound_map[SOUND_SNIPER_FIRE] = LoadSound("res/sniper.ogg");
    sound_map[SOUND_NADE_EXPLOSION] = LoadSound("res/explosion.ogg");
    sound_map[SOUND_STEP] = LoadSound("res/step.ogg");
}

void audio_deinit() {
    for (u32 i = 0; i < SOUND_COUNT; ++i) {
        UnloadSound(sound_map[i]);
    }

    CloseAudioDevice();
}

void audio_play_spatial_sound(enum sound sound, v2 sound_pos, v2 observer_pos, v2 observer_dir) {
    v2 delta = v2sub(observer_pos, sound_pos);

    const f32 dist = v2len(delta);
    const f32 base_volume = sound_base_volume[sound];
    const f32 volume = f32_min(base_volume/dist, base_volume);
    SetSoundVolume(sound_map[sound], volume);

    v2 orthogonal_dir = {-observer_dir.y, observer_dir.x};
    f32 orthogonal_amount = (dist > 0.5f) ? v2dot(delta, orthogonal_dir) / dist : 0.0f;
    SetSoundPan(sound_map[sound], 0.5f + 0.5f*orthogonal_amount);

    PlaySound(sound_map[sound]);
}
