#pragma once
#include <stdint.h>
int media_init(void);
void media_shutdown(void (*heartbeat)(void));
void media_play(unsigned frame);
void media_pause(void);
void media_volume_step(int delta);
unsigned media_hud_token(void);
void media_overlay(uint8_t *ranks);
int media_volume(void);

