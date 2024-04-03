#ifndef GB3_AUDIO_DMA_H
#define GB3_AUDIO_DMA_H

#include <stdint.h>
void dmaAudio();
static void dma_handler_buffer0();
static void dma_handler_buffer1();
void playAudio(uint16_t *audio_to_play, int newPriority, uint16_t numSamples);
void stopAudio();
void serviceAudio();
bool fillAudioBuffer(int whichOne);
void dmaAudioInit();

#endif