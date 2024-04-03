#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "gb3_audio_dma.h"

uint32_t audioSamples = 0;
uint32_t samplesToExpect = 0;
uint8_t whichAudioBuffer = 0;
bool audioPlaying = false;

#define audioBufferSize	1024
uint32_t audioBuffer0[audioBufferSize] __attribute__((aligned(audioBufferSize)));
uint32_t audioBuffer1[audioBufferSize] __attribute__((aligned(audioBufferSize)));

bool fillBuffer0flag = false;
bool fillBuffer1flag = false;
bool endAudioBuffer0flag = false;
bool endAudioBuffer1flag = false;
int audio_pin_slice;
int currentAudioPriority = 0;
uint16_t *audio_data;
int c0_chan=1, c1_chan=2;
int audio_pin = 6;
uint16_t vol = 256; //8.8 (256=1.0)
uint16_t buffer0size = 0;
uint16_t buffer1size = 0;

void dmaAudioInit() {
    gpio_set_function(audio_pin, GPIO_FUNC_PWM);
    pwm_set_gpio_level(audio_pin, 128);
    audio_pin_slice = pwm_gpio_to_slice_num(audio_pin);	
	pwm_config config = pwm_get_default_config();
	
    uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    float clock_div = ((float)f_clk_sys * 1000.0f) / 255.0f / 44100.0f / 16.0f;

	pwm_config_set_clkdiv(&config, clock_div); 
    pwm_config_set_wrap(&config, 255); 
    pwm_init(audio_pin_slice, &config, true);

    //c0_chan = dma_claim_unused_channel(true);
    //c1_chan = dma_claim_unused_channel(true);
}

void dmaAudio() {

    dma_channel_unclaim(c0_chan);
	
    dma_channel_config c0 = dma_channel_get_default_config(c0_chan);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);	

    //dma_timer_set_fraction(0, 0x0017, 0xffff);			//The timer will run at the system_clock_freq * numerator / denominator, so this is the speed that data elements will be transferred at via a DMA channel using this timer as a DREQ
    dma_timer_set_fraction(0, 0x0001, 0x179d);
    channel_config_set_dreq(&c0, 0x3b);                                 // DREQ paced by timer 0     // 0x3b means timer0 (see SDK manual)
	channel_config_set_chain_to(&c0, c1_chan); 
	
	
    dma_channel_configure(c0_chan,
						  &c0,
						  &pwm_hw->slice[audio_pin_slice].cc,			//dst
                          audioBuffer0,   // src
                          samplesToExpect, //audioBufferSize,  // transfer count
                          false           // Do not start immediately
    );

	dma_channel_set_irq0_enabled(c0_chan, true);
    while (irq_has_shared_handler(DMA_IRQ_0)) {                         // Remove any shared IRQ handlers that other devices might be using (this will break other things)       
        irq_handler_t h = irq_get_vtable_handler(DMA_IRQ_0);
        irq_remove_handler(DMA_IRQ_0, h);
    }
	irq_set_exclusive_handler(DMA_IRQ_0, dma_handler_buffer0);
	irq_set_enabled(DMA_IRQ_0, true);   

    dma_channel_unclaim(c1_chan);
	
    dma_channel_config c1 = dma_channel_get_default_config(c1_chan);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
    channel_config_set_read_increment(&c1, true);
    channel_config_set_write_increment(&c1, false);	
    channel_config_set_dreq(&c1, 0x3b);                                 // DREQ paced by timer 0     // 0x3b means timer0 (see SDK manual)
	channel_config_set_chain_to(&c1, c0_chan); 
	
    dma_channel_configure(c1_chan,
							&c1,
							&pwm_hw->slice[audio_pin_slice].cc,			//dst
							audioBuffer1,   // src
							samplesToExpect, //audioBufferSize,  // transfer count
							false           // Do not start immediately
    );

	dma_channel_set_irq1_enabled(c1_chan, true);
	irq_set_exclusive_handler(DMA_IRQ_1, dma_handler_buffer1);
	irq_set_enabled(DMA_IRQ_1, true);  

    dma_start_channel_mask(1u << c0_chan);
}

static void dma_handler_buffer0() {		//This is called when DMA audio block0 finishes

	if (endAudioBuffer0flag == true) {	
		endAudioBuffer0flag = false;
		audioPlaying =  false;
		pwm_set_gpio_level(audio_pin, 128);
		dma_channel_abort(c1_chan);			//Kill the other channel
	}
	else {
		dma_channel_set_read_addr(c0_chan, audioBuffer0, false);
		fillBuffer0flag = true;						//Set flag that buffer needs re-filled while other DMA runs		
	}

    // Clear interrupt for trigger DMA channel.
    dma_hw->ints0 = (1u << c0_chan);
    
}

static void dma_handler_buffer1() {     //This is called when DMA audio block1 finishes

	if (endAudioBuffer1flag == true) {
		endAudioBuffer1flag = false;
		audioPlaying =  false;
		pwm_set_gpio_level(audio_pin, 128);
		dma_channel_abort(c0_chan);			//Kill the other channel
	}
	else {
		dma_channel_set_read_addr(c1_chan, audioBuffer1, false);
		fillBuffer1flag = true;						//Set flag that buffer needs re-filled while other DMA runs		
	}
    // Clear interrupt for trigger DMA channel.
    dma_hw->ints0 = (1u << c1_chan);
    
} 

//Plays a 11025Hz 8-bit mono WAVE file from file system using the PWM function and DMA on GPIO14 (gamebadge channel 4)
void playAudio(uint16_t *audio_to_play, int newPriority, uint16_t numSamples) {
	
	//if (audioPlaying == true) {			//Only one sound at a time
		//file.close();
		
		//WKM if (newPriority >= currentAudioPriority) {
		//WKM	stopAudio();
		//WKM }
		//WKM else {
		//WKM 	return;
		//WKM }
	//}
	
	//if (!file.open(path, O_RDONLY)) {	//Abort if Don Bot's mercy file isn't found
	//	return;
	//}
    audio_data = audio_to_play;         // Set the global pointer so we can access the audio data from the buffer filling function
    
	//file.seekSet(40);				    //Jump to audio sample size part of WAVE file

	//audioSamples = file.read() | (file.read() << 8) | (file.read() << 16) | (file.read() << 24);	//Get # of samples
	samplesToExpect = numSamples;				// Store the number of samples we expect to receive from the APU in the stream (this should be consistent)
    audioSamples = samplesToExpect;				// This variable will be used to track whether we need to refill the buffers
	
	fillAudioBuffer(0);							//Fill both buffers to start
	fillAudioBuffer(1);

	whichAudioBuffer = 0;						//DMA starts with buffer 0
	audioPlaying = true;						//Flag that audio is playing

	endAudioBuffer0flag =  false;
	endAudioBuffer1flag =  false;
	fillBuffer0flag = false;
	fillBuffer1flag = false;

	dmaAudio();									//Start the audio DMA

}

void stopAudio() {

	if (audioPlaying == false) {
		return;
	}

	//WKM file.close();	
	
	//Kill interrupts and channels
	dma_hw->ints0 = (1u << c1_chan);
	dma_channel_abort(c1_chan);			//Kill the other channel
	dma_hw->ints0 = (1u << c0_chan);
	dma_channel_abort(c0_chan);			//Kill the other channel
	
}

void serviceAudio() {					//This is called every game frame to see if PCM audio buffers need re-loading

	if (audioPlaying == false) {
		return;
	}
	
	if (fillBuffer0flag == true) {
		fillBuffer0flag = false;
		endAudioBuffer0flag = fillAudioBuffer(0);			//Fill buffer and return flag if we loaded the last of data
	}
	if (fillBuffer1flag == true) {
		fillBuffer1flag = false;
		endAudioBuffer1flag = fillAudioBuffer(1);			//Fill buffer and return flag if we loaded the last of data
	}	
	
}

bool fillAudioBuffer(int whichOne) {

	int samplesToLoad = audioBufferSize;			//Default # of samples to load into buffer

	//What if samples on 512 edge?

	//gpio_put(15, 1);

	if (audioSamples < audioBufferSize) {			//Are there fewer samples than a full buffer size?
		samplesToLoad = audioSamples;	
	}

	if (whichOne == 0) {							//Load selected buffer
		for (int x = 0 ; x < samplesToLoad ; x++) {
			//WKM audioBuffer0[x] = file.read();
            // int temp = audio_data[x<<1]>>8;
            // temp = temp + 128;
            // if (temp > 255) {
            //     temp = 255;
            // }
            audioBuffer0[x] = audio_data[(x<<1)+1]>>8;     // Multiply by 2 in order to transfer every other sample (left channel only)
			audioBuffer0[x] += audio_data[(x<<1)]>>8;
            audioBuffer0[x] = (audioBuffer0[x] * vol) >> 8;  // Multiply by volume and divide by 256
			if (audioBuffer0[x] > 255) {
				audioBuffer0[x] = 255;
			}
		}
		
		
		//file.read(audioBuffer0, samplesToLoad);
	}
	else {
		for (int x = 0 ; x < samplesToLoad ; x++) {
			//WKM audioBuffer1[x] = file.read();
            // int temp = audio_data[x<<1]>>8;
            // temp = temp + 128;
            // if (temp > 255) {
            //     temp = 255;
            // }
            audioBuffer1[x] = audio_data[(x<<1)+1]>>8;   // Multiply by 2 in order to transfer every other sample (left channel only)
			audioBuffer1[x] += audio_data[(x<<1)]>>8;
            audioBuffer1[x] = (audioBuffer1[x] * vol) >> 8;  // Multiply by volume and divide by 256
			if (audioBuffer1[x] > 255) {
				audioBuffer1[x] = 255;
			}

		}		
		
		//file.read(audioBuffer1, samplesToLoad);
	}
	
	//gpio_put(15, 0);
	
	if (samplesToLoad < audioBufferSize) {			//Not a full buffer? Means end of file, this is final DMA to do
		audioSamples = samplesToExpect;				// Reset the sample counter
		//audioSamples = 0;							//Zero this out
		//WKM file.close();								//Close file		
		dma_channel_set_trans_count(whichOne, samplesToLoad, false);	//When this channel re-triggers only beat out the exact # of bytes we have
		//dma_channel_config object = dma_channel_get_default_config(whichOne + 1);		//Get reference object for channel	
		//channel_config_set_chain_to(&object, whichOne + 1);		//Set this channel to chain to itself next time it finishes, which disables chaining
		//return true;
		return false;
	}
	
	audioSamples -= samplesToLoad;				//Else dec # of samples and return no flag

	return false;

}