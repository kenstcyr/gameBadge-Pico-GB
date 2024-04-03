#ifndef AUDIO_H_FILE
#define AUDIO_H_FILE

#define AUDIO_BUFFER_SIZE 512
#define AUDIO_MAX_SOURCES 2

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init2(int audio_pin, int sample_freq);
uint8_t *audio_get_buffer(void);

int audio_play_once(const uint16_t *samples, int len);
int audio_play_loop(const uint16_t *samples, int len, int loop_start);

void audio_source_stop(int source_id);
void audio_source_set_volume(int source_id, uint16_t volume);

void audio_mixer_step(void);

uint8_t Convert16To8(uint16_t sample);


#endif /* AUDIO_H_FILE */
