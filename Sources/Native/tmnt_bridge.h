#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {TMNT_COIN=1, TMNT_START=2, TMNT_ATTACK=4, TMNT_JUMP=8};
void *tmnt_create(const char *assets, const char *saves);
void tmnt_destroy(void *);
int tmnt_reset(void *);
const char *tmnt_error(void *);
int tmnt_step(void *, float x, float y, float unused, uint32_t buttons);
void tmnt_set_invincible(void *, int);
int tmnt_get_invincible(void *);
void tmnt_set_player(void *, int player);
int tmnt_get_player(void *);
const uint8_t *tmnt_pixels(void *);
const int16_t *tmnt_audio(void *);
int tmnt_audio_count(void *);
int tmnt_width(void *);
int tmnt_height(void *);
double tmnt_frame_rate(void *);
uint64_t tmnt_frame_number(void *);
const uint32_t *tmnt_state(void *);
int tmnt_state_size(void *);
const uint8_t *tmnt_ram(void *);
int tmnt_ram_size(void *);
#ifdef __cplusplus
}
#endif
