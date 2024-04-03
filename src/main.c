/**
 * Copyright (C) 2022 by Mahyar Koshkouei <mk@deltabeard.com>
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

// Peanut-GB emulator settings
#define ENABLE_SOUND	1
#define ENABLE_SDCARD	1
#define PEANUT_GB_HIGH_LCD_ACCURACY 1
#define PEANUT_GB_USE_BIOS 0
#define USE_GB3_AUDIO_LIB 0
#define AUDIO_PWM 0

/* C Headers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* RP2040 Headers */
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/spi.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
#include <hardware/vreg.h>
#include <pico/bootrom.h>
#include <pico/stdio.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <sys/unistd.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>
#include <hardware/flash.h>
#include <pico/time.h>

/* Project headers */
#include "hedley.h"
#include "minigb_apu.h"
#include "peanut_gb.h"
#include "pico_ST7789.h"
#include "gbcolors.h"
#include "audio.h"
#include "gb3_audio_dma.h"
#include "config.h"
#include "sdcard.h"
#include "lcd_dma.h"

// ST7789 Configuration
const struct st7789_config lcd_config = {
    .spi      = PICO_DEFAULT_SPI_INSTANCE,
    .gpio_din = GPIO_SDA,
    .gpio_clk = GPIO_CLK,
    .gpio_cs  = GPIO_CS,
    .gpio_dc  = GPIO_RS,
    .gpio_rst = GPIO_RST,
    .gpio_bl  = GPIO_LED
};

#if ENABLE_SOUND
/**
 * Global variables for audio task
 * stream contains N=AUDIO_SAMPLES samples
 * each sample is 32 bits
 * 16 bits for the left channel + 16 bits for the right channel in stereo interleaved format)
 * This is intended to be played at AUDIO_SAMPLE_RATE Hz
 */
uint16_t *stream;
//static uint16_t stream[1098];
int volume = 256;
// struct repeating_timer timerWFGenerator;    // Timer for the waveform generator - 125KHz
int audiopos =0;
// bool wavegen_callback(struct repeating_timer *t) {
// 	//audio_mixer_step();
//     return true;
// }

#if AUDIO_PWM
void pwm_interrupt_handler() {
    pwm_clear_irq(pwm_gpio_to_slice_num(GPIO_AUDIO));    
	if (audiopos < (AUDIO_SAMPLES<<3) - 1) {

		pwm_set_gpio_level(GPIO_AUDIO, stream[(audiopos+1)>>3]>>8);
		audiopos+=2;
	} else {
		audiopos = 0;
	}
}
#endif
#endif

/** Definition of ROM data
 * We're going to erase and reprogram a region 1Mb from the start of the flash
 * Once done, we can access this at XIP_BASE + 1Mb.
 * Game Boy DMG ROM size ranges from 32768 bytes (e.g. Tetris) to 1,048,576 bytes (e.g. Pokemod Red)
 */
#define FLASH_TARGET_OFFSET (1024 * 1024)
static uint8_t ram[32768];
static unsigned char rom_bank0[65536];
const uint8_t *rom = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr);
uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr);
void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,const uint8_t val);
void read_cart_ram_file(struct gb_s *gb);
void write_cart_ram_file(struct gb_s *gb);
void load_cart_rom_file(char *filename);
uint16_t rom_file_selector_display_page(char filename[22][256],uint16_t num_page);
void rom_file_selector();

static int lcd_line_busy = 0;
static palette_t palette;						// Colour palette
static uint8_t manual_palette_selected=0;
static uint8_t lcd_scaling = 1;
static uint8_t pixels_buffer[LCD_WIDTH];		// Pixel data is stored in here.
static struct
{
	unsigned a	: 1;
	unsigned b	: 1;
	unsigned select	: 1;
	unsigned start	: 1;
	unsigned right	: 1;
	unsigned left	: 1;
	unsigned up	: 1;
	unsigned down	: 1;
} prev_joypad_bits;

/* Multicore command structure. */
union core_cmd {
    struct {
		#define CORE_CMD_NOP		0		// Does nothing.
		#define CORE_CMD_LCD_LINE	1		// Set line "data" on the LCD. Pixel data is in pixels_buffer.
		uint8_t cmd;
		uint8_t unused1;
		uint8_t unused2;
		uint8_t data;
    };
    uint32_t full;
};

/**
 * Ignore all errors.
 */
void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t addr)
{
#if 1
	const char* gb_err_str[4] = {
			"UNKNOWN",
			"INVALID OPCODE",
			"INVALID READ",
			"INVALID WRITE"
		};
	printf("Error %d occurred: %s at %04X\n.\n", gb_err, gb_err_str[gb_err], addr);
//	abort();
#endif
}

void core1_lcd_draw_line(const uint_fast8_t line)
{
	static uint16_t fb[LCD_WIDTH];										// 16-bit frame buffer
	static uint16_t scaledLineBuffer[SCREEN_WIDTH];
	memset(scaledLineBuffer, 0, sizeof(scaledLineBuffer));				// Clear the scaled line buffer

	for(unsigned int x = 0; x < LCD_WIDTH; x++)
	{
		fb[x] = palette[(pixels_buffer[x] & LCD_PALETTE_ALL) >> 4]
				[pixels_buffer[x] & 3];

		if (lcd_scaling) {
			scaledLineBuffer[x*3/2] = fb[x];							// Fill the scaled buffer with pixel skipping
			if (lcd_scaling == 2) scaledLineBuffer[(x*3/2)-1] = fb[x];	// Fill in skipped pixels
		} else {
			scaledLineBuffer[x+40] = fb[x];								// Fill the scaled buffer with an offset
		}
	}

	if (lcd_scaling) {													// If we're scaling...
		st7789_raset(line*3/2, SCREEN_HEIGHT-1);						// Skip every other line
	} else {															// If we're not scaling...
	 	st7789_raset(line+48, SCREEN_HEIGHT-1);							// Set the line with an offset from the top of the display
	}
	st7789_ramwr();
	st7789_write_pixels(scaledLineBuffer, SCREEN_WIDTH);				// Write the scaled line buffer to the display
	if (lcd_scaling == 2 && line % 2 != 0) {							// If we're on an odd line...
		st7789_write_pixels(scaledLineBuffer, SCREEN_WIDTH);			// Write the scaled line buffer to the display
	}

	__atomic_store_n(&lcd_line_busy, 0, __ATOMIC_SEQ_CST);
}

_Noreturn
void main_core1(void)
{
	union core_cmd cmd;

	/* Initialise and control LCD on core 1. */
	st7789_init(&lcd_config, SCREEN_WIDTH, SCREEN_HEIGHT);				// Initialize ST7789 display
	st7789_setRotation(1);												// Ribbon cable on left side of display
	st7789_backlight(true);												// Turn on the backlight
	st7789_fill(0xFFFF);												// Clear LCD screen
	st7789_fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x0000);			// Clear the portion of the screen with the emulator window

	/* Handle commands coming from core0. */
	while(1)
	{
		 cmd.full = multicore_fifo_pop_blocking();						// Pull data off the queue
		switch(cmd.cmd)
		{
			case CORE_CMD_LCD_LINE:										// We're being told to draw a line
				core1_lcd_draw_line(cmd.data);							// Draw the line
				break;

			case CORE_CMD_NOP:
			default:
				break;
		}
	}

	HEDLEY_UNREACHABLE();
}

void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[LCD_WIDTH], const uint_fast8_t line)
{
	union core_cmd cmd;

	/* Wait until previous line is sent. */
	while(__atomic_load_n(&lcd_line_busy, __ATOMIC_SEQ_CST))
		tight_loop_contents();

	memcpy(pixels_buffer, pixels, LCD_WIDTH);
	
	/* Populate command. */
	cmd.cmd = CORE_CMD_LCD_LINE;
	cmd.data = line;

	__atomic_store_n(&lcd_line_busy, 1, __ATOMIC_SEQ_CST);
	multicore_fifo_push_blocking(cmd.full);
}


int main(void)
{
	static struct gb_s gb;
	enum gb_init_error_e ret;
	
	/* Overclock. */
	{
		const unsigned vco = 1596*1000*1000;	/* 266MHz */
		const unsigned div1 = 6, div2 = 1;

		vreg_set_voltage(VREG_VOLTAGE_1_15);
		sleep_ms(2);
		set_sys_clock_pll(vco, div1, div2);
		sleep_ms(2);
	}

	/* Initialise USB serial connection for debugging. */
	stdio_init_all();
	time_init();

	/* Initialise GPIO pins. */
	gpio_set_function(GPIO_UP, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_DOWN, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_LEFT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_RIGHT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_A, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_B, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_SELECT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_START, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_CS, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_CLK, GPIO_FUNC_SPI);
	gpio_set_function(GPIO_SDA, GPIO_FUNC_SPI);
	gpio_set_function(GPIO_RS, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_RST, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_LED, GPIO_FUNC_SIO);

	gpio_set_dir(GPIO_UP, false);
	gpio_set_dir(GPIO_DOWN, false);
	gpio_set_dir(GPIO_LEFT, false);
	gpio_set_dir(GPIO_RIGHT, false);
	gpio_set_dir(GPIO_A, false);
	gpio_set_dir(GPIO_B, false);
	gpio_set_dir(GPIO_SELECT, false);
	gpio_set_dir(GPIO_START, false);
	gpio_set_dir(GPIO_CS, true);
	gpio_set_dir(GPIO_RS, true);
	gpio_set_dir(GPIO_RST, true);
	gpio_set_dir(GPIO_LED, true);
	gpio_set_slew_rate(GPIO_CLK, GPIO_SLEW_RATE_FAST);
	gpio_set_slew_rate(GPIO_SDA, GPIO_SLEW_RATE_FAST);
	
	gpio_pull_up(GPIO_UP);
	gpio_pull_up(GPIO_DOWN);
	gpio_pull_up(GPIO_LEFT);
	gpio_pull_up(GPIO_RIGHT);
	gpio_pull_up(GPIO_A);
	gpio_pull_up(GPIO_B);
	gpio_pull_up(GPIO_SELECT);
	gpio_pull_up(GPIO_START);

	/* Set SPI clock to use high frequency. */
	clock_configure(clk_peri, 0,
			CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
			125 * 1000 * 1000, 125 * 1000 * 1000);
	spi_init(spi0, 62*1000*1000);
	spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);


	#if ENABLE_SOUND
		gpio_init(26);								//Audio amp enable (active HIGH)
		gpio_set_dir(26, GPIO_OUT);
		gpio_put(26, 1);

		#if AUDIO_PWM
			gpio_set_function(GPIO_AUDIO, GPIO_FUNC_PWM);
			int audio_pin_slice = pwm_gpio_to_slice_num(GPIO_AUDIO);

			// Setup PWM interrupt to fire when PWM cycle is complete
			pwm_clear_irq(audio_pin_slice);
			pwm_set_irq_enabled(audio_pin_slice, true);
			// set the handle function above
			irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_interrupt_handler); 
			irq_set_enabled(PWM_IRQ_WRAP, true);

			pwm_config config = pwm_get_default_config();
			pwm_config_set_clkdiv(&config, 1.0f); 
			pwm_config_set_wrap(&config, 254); 
			pwm_init(audio_pin_slice, &config, true);
			pwm_set_gpio_level(GPIO_AUDIO, 0);
		#endif

		// Allocate memory for the stream buffer
		//stream=malloc(AUDIO_BUFFER_SIZE_BYTES);
		//assert(stream!=NULL);
		//memset(stream,0,AUDIO_BUFFER_SIZE_BYTES);  // Zero out the stream buffer
		stream=malloc(2048);
		assert(stream!=NULL);
		memset(stream,0,2048);  // Zero out the stream buffer

		// #if !USE_GB3_AUDIO_LIB
		 	audio_init2(GPIO_AUDIO, AUDIO_SAMPLE_RATE);
		 	audio_source_set_volume(0, volume);
		 	audio_play_loop(stream, 512, 0);
		// #endif

		//add_repeating_timer_us(-64, wavegen_callback, NULL, &timerWFGenerator);
	#endif

	while(true)
	{
		#if ENABLE_SDCARD
			/* ROM File selector */
			st7789_init(&lcd_config, SCREEN_WIDTH, SCREEN_HEIGHT);	// Initialize ST7789 display
			st7789_setRotation(1);									// Ribbon cable on left side of display
			st7789_backlight(true);									// Turn on the backlight

			st7789_fill(0x0000);

			rom_file_selector();
		#endif

		/* Initialise GB context. */
		memcpy(rom_bank0, rom, sizeof(rom_bank0));
		ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read, &gb_cart_ram_write, &gb_error, NULL);

		if(ret != GB_INIT_NO_ERROR)
		{
			printf("Error: %d\n", ret);
			goto out;
		}

		manual_assign_palette(palette, 12);						// Set to color palette 12 (original GB green)
		#if AUTO_PALETTE
			/* Automatically assign a colour palette to the game */
			char rom_title[16];
			auto_assign_palette(palette, gb_colour_hash(&gb),gb_get_rom_name(&gb,rom_title));
		#endif
	
		gb_init_lcd(&gb, &lcd_draw_line);
		multicore_launch_core1(main_core1);				// Start Core1, which processes requests to the LCD

		#if ENABLE_SOUND
			// Initialize audio emulation
			audio_init();

			#if USE_GB3_AUDIO_LIB
				dmaAudioInit();
				playAudio(stream, 0, AUDIO_SAMPLES);
			#endif
		#endif

		#if ENABLE_SDCARD
			/* Load Save File. */
			//WKM read_cart_ram_file(&gb);
		#endif

		uint_fast32_t frames = 0;
		uint64_t start_time = time_us_64();
		while(1)
		{
			int input;

			gb.gb_frame = 0;
			
			do {
				__gb_step_cpu(&gb);
				tight_loop_contents();
			} while(HEDLEY_LIKELY(gb.gb_frame == 0));

			frames++;
			#if ENABLE_SOUND
				if(!gb.direct.frame_skip) {
					audio_callback(NULL, stream, 2048);
					//audio_callback(NULL, stream, 1098);
					//UpdateAudioBuffer(stream, AUDIO_SAMPLES);

					#if USE_GB3_AUDIO_LIB
						serviceAudio();
					#else
						audio_mixer_step();
					#endif
					//i2s_dma_write(&i2s_config, stream);
					//audio_play_once(stream, AUDIO_SAMPLES);
					//audio_mixer_step();
					//pwm_set_gpio_level(GPIO_AUDIO, stream[sample_rate]);
					//sample_rate++;
					//uint slice_num = slice_number;			//Do this once, less math
					//uint chan = chan_nummber;

					//pwm_set_clkdiv_int_frac(slice_num, 1, 0);
					//pwm_set_wrap(slice_num, 8000);
					//pwm_set_chan_level(slice_num, chan, *stream);
					//pwm_set_enabled(slice_num, true);
					
				}
			#endif

			/* Update buttons state */
			prev_joypad_bits.up=gb.direct.joypad_bits.up;
			prev_joypad_bits.down=gb.direct.joypad_bits.down;
			prev_joypad_bits.left=gb.direct.joypad_bits.left;
			prev_joypad_bits.right=gb.direct.joypad_bits.right;
			prev_joypad_bits.a=gb.direct.joypad_bits.a;
			prev_joypad_bits.b=gb.direct.joypad_bits.b;
			prev_joypad_bits.select=gb.direct.joypad_bits.select;
			prev_joypad_bits.start=gb.direct.joypad_bits.start;
			gb.direct.joypad_bits.up=gpio_get(GPIO_UP);
			gb.direct.joypad_bits.down=gpio_get(GPIO_DOWN);
			gb.direct.joypad_bits.left=gpio_get(GPIO_LEFT);
			gb.direct.joypad_bits.right=gpio_get(GPIO_RIGHT);
			gb.direct.joypad_bits.a=gpio_get(GPIO_A);
			gb.direct.joypad_bits.b=gpio_get(GPIO_B);
			gb.direct.joypad_bits.select=gpio_get(GPIO_SELECT);
			gb.direct.joypad_bits.start=gpio_get(GPIO_START);

			/* hotkeys (select + * combo)*/
			if(!gb.direct.joypad_bits.select) {
				#if ENABLE_SOUND
					if(!gb.direct.joypad_bits.up && prev_joypad_bits.up) {
						/* select + up: increase sound volume */
						//i2s_increase_volume(&i2s_config);
						volume+=16;
						//audio_source_set_volume(0, volume);
					}
					if(!gb.direct.joypad_bits.down && prev_joypad_bits.down) {
						/* select + down: decrease sound volume */
						//i2s_decrease_volume(&i2s_config);
						volume-=16;
						//audio_source_set_volume(0, volume);
					}
				#endif
				if(!gb.direct.joypad_bits.right && prev_joypad_bits.right) {
					/* select + right: select the next manual color palette */
					if(manual_palette_selected<12) {
						manual_palette_selected++;
						manual_assign_palette(palette,manual_palette_selected);
					}	
				}
				if(!gb.direct.joypad_bits.left && prev_joypad_bits.left) {
					/* select + left: select the previous manual color palette */
					if(manual_palette_selected>0) {
						manual_palette_selected--;
						manual_assign_palette(palette,manual_palette_selected);
					}
				}
				if(!gb.direct.joypad_bits.start && prev_joypad_bits.start) {
					/* select + start: save ram and resets to the game selection menu */
					#if ENABLE_SDCARD				
						write_cart_ram_file(&gb);
					#endif				
					goto out;
				}
				if(!gb.direct.joypad_bits.a && prev_joypad_bits.a) {
					/* select + A: enable/disable frame-skip => fast-forward */
					gb.direct.frame_skip=!gb.direct.frame_skip;
					printf("I gb.direct.frame_skip = %d\n",gb.direct.frame_skip);
				}
				if (!gb.direct.joypad_bits.b && prev_joypad_bits.b) {
					/* select + B: Toggle Scaling */
					while (lcd_line_busy) {true; };
					st7789_fill(0x0000);				// Clear the screen
					lcd_scaling++;
					if (lcd_scaling > 1) lcd_scaling = 0;	// Toggle scaling
					printf("I scaling toggled\n");
				}	
			}

			/* Serial monitor commands */ 
			input = getchar_timeout_us(0);
			if(input == PICO_ERROR_TIMEOUT) continue;

			switch(input)
			{
				case 'i':
					gb.direct.interlace = !gb.direct.interlace;
					break;

				case 'f':
					gb.direct.frame_skip = !gb.direct.frame_skip;
					break;

				case 'b':
				{
					uint64_t end_time;
					uint32_t diff;
					uint32_t fps;

					end_time = time_us_64();
					diff = end_time-start_time;
					fps = ((uint64_t)frames*1000*1000)/diff;
					printf("Frames: %u\n"
						"Time: %lu us\n"
						"FPS: %lu\n",
						frames, diff, fps);
					stdio_flush();
					frames = 0;
					start_time = time_us_64();
					break;
				}

				case '\n':
				case '\r':
				{
					gb.direct.joypad_bits.start = 0;
					break;
				}

				case '\b':
				{
					gb.direct.joypad_bits.select = 0;
					break;
				}

				case '8':
				{
					gb.direct.joypad_bits.up = 0;
					break;
				}

				case '2':
				{
					gb.direct.joypad_bits.down = 0;
					break;
				}

				case '4':
				{
					gb.direct.joypad_bits.left= 0;
					break;
				}

				case '6':
				{
					gb.direct.joypad_bits.right = 0;
					break;
				}

				case 'z':
				case 'w':
				{
					gb.direct.joypad_bits.a = 0;
					break;
				}

				case 'x':
				{
					gb.direct.joypad_bits.b = 0;
					break;
				}

				case 'q':
					goto out;

				default:
					break;
			}
		}
		out:
			puts("\nEmulation Ended");
			multicore_reset_core1(); 				// stop lcd task running on core 1
	}
}

/**
 * Returns a byte from the ROM file at the given address.
 */
uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
	(void) gb;
	if(addr < sizeof(rom_bank0))
		return rom_bank0[addr];

	return rom[addr];
}

/**
 * Returns a byte from the cartridge RAM at the given address.
 */
uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
	(void) gb;
	return ram[addr];
}

/**
 * Writes a given byte to the cartridge RAM at the given address.
 */
void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val)
{
	ram[addr] = val;
}
#if ENABLE_SDCARD
/**
 * Load a save file from the SD card
 */
void read_cart_ram_file(struct gb_s *gb) {
	char filename[16];
	uint_fast32_t save_size;
	UINT br;
	
	gb_get_rom_name(gb,filename);
	save_size=gb_get_save_size(gb);
	if(save_size>0) {
		sd_card_t *pSD=sd_get_by_num(0);
		FRESULT fr=f_mount(&pSD->fatfs,pSD->pcName,1);
		if (FR_OK!=fr) {
			printf("E f_mount error: %s (%d)\n",FRESULT_str(fr),fr);
			return;
		}

		FIL fil;
		fr=f_open(&fil,filename,FA_READ);
		if (fr==FR_OK) {
			f_read(&fil,ram,f_size(&fil),&br);
		} else {
			printf("E f_open(%s) error: %s (%d)\n",filename,FRESULT_str(fr),fr);
		}
		
		fr=f_close(&fil);
		if(fr!=FR_OK) {
			printf("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
		}
		f_unmount(pSD->pcName);
	}
	printf("I read_cart_ram_file(%s) COMPLETE (%lu bytes)\n",filename,save_size);
}

/**
 * Write a save file to the SD card
 */
void write_cart_ram_file(struct gb_s *gb) {
	char filename[16];
	uint_fast32_t save_size;
	UINT bw;
	
	gb_get_rom_name(gb,filename);
	save_size=gb_get_save_size(gb);
	if(save_size>0) {
		sd_card_t *pSD=sd_get_by_num(0);
		FRESULT fr=f_mount(&pSD->fatfs,pSD->pcName,1);
		if (FR_OK!=fr) {
			printf("E f_mount error: %s (%d)\n",FRESULT_str(fr),fr);
			return;
		}

		FIL fil;
		fr=f_open(&fil,filename,FA_CREATE_ALWAYS | FA_WRITE);
		if (fr==FR_OK) {
			f_write(&fil,ram,save_size,&bw);
		} else {
			printf("E f_open(%s) error: %s (%d)\n",filename,FRESULT_str(fr),fr);
		}
		
		fr=f_close(&fil);
		if(fr!=FR_OK) {
			printf("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
		}
		f_unmount(pSD->pcName);
	}
	printf("I write_cart_ram_file(%s) COMPLETE (%lu bytes)\n",filename,save_size);
}

/**
 * Load a .gb rom file in flash from the SD card 
 */ 
void load_cart_rom_file(char *filename) {
	UINT br;
	uint8_t buffer[FLASH_SECTOR_SIZE];
	bool mismatch=false;
	sd_card_t *pSD=sd_get_by_num(0);
	FRESULT fr=f_mount(&pSD->fatfs,pSD->pcName,1);
	if (FR_OK!=fr) {
		printf("E f_mount error: %s (%d)\n",FRESULT_str(fr),fr);
		return;
	}
	FIL fil;
	fr=f_open(&fil,filename,FA_READ);
	if (fr==FR_OK) {
		uint32_t flash_target_offset=FLASH_TARGET_OFFSET;
		for(;;) {
			f_read(&fil,buffer,sizeof buffer,&br);
			if(br==0) break; /* end of file */

			printf("I Erasing target region...\n");
			flash_range_erase(flash_target_offset,FLASH_SECTOR_SIZE);
			printf("I Programming target region...\n");
			flash_range_program(flash_target_offset,buffer,FLASH_SECTOR_SIZE);
			
			/* Read back target region and check programming */
			printf("I Done. Reading back target region...\n");
			for(uint32_t i=0;i<FLASH_SECTOR_SIZE;i++) {
				if(rom[flash_target_offset+i]!=buffer[i]) {
					mismatch=true;
				}
			}

			/* Next sector */
			flash_target_offset+=FLASH_SECTOR_SIZE;
		}
		if(mismatch) {
	        printf("I Programming successful!\n");
		} else {
			printf("E Programming failed!\n");
		}
	} else {
		printf("E f_open(%s) error: %s (%d)\n",filename,FRESULT_str(fr),fr);
	}
	
	fr=f_close(&fil);
	if(fr!=FR_OK) {
		printf("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
	}
	f_unmount(pSD->pcName);

	printf("I load_cart_rom_file(%s) COMPLETE (%lu bytes)\n",filename,br);
}

/**
 * Function used by the rom file selector to display one page of .gb rom files
 */
uint16_t rom_file_selector_display_page(char filename[22][256],uint16_t num_page) {
	sd_card_t *pSD=sd_get_by_num(0);
    DIR dj;
    FILINFO fno;
    FRESULT fr;

    fr=f_mount(&pSD->fatfs,pSD->pcName,1);
    if (FR_OK!=fr) {
        printf("E f_mount error: %s (%d)\n",FRESULT_str(fr),fr);
        return 0;
    }

	/* clear the filenames array */
	for(uint8_t ifile=0;ifile<22;ifile++) {
		strcpy(filename[ifile],"");
	}

    /* search *.gb files */
	uint16_t num_file=0;
	fr=f_findfirst(&dj, &fno, "\\gb", "*.gb");

	/* skip the first N pages */
	if(num_page>0) {
		while(num_file<num_page*22 && fr == FR_OK && fno.fname[0]) {
			num_file++;
			fr=f_findnext(&dj, &fno);
		}
	}

	/* store the filenames of this page */
	num_file=0;
    while(num_file<22 && fr == FR_OK && fno.fname[0]) {
		strcpy(filename[num_file],fno.fname);
        num_file++;
        fr=f_findnext(&dj, &fno);
    }
	f_closedir(&dj);
	f_unmount(pSD->pcName);

	/* display *.gb rom files on screen */
	st7789_fill(0x0000);
	for(uint8_t ifile=0;ifile<num_file;ifile++) {
		st7789_text(filename[ifile],0,ifile*8,0xFFFF,0x0000);
    }
	return num_file;
}

/**
 * The ROM selector displays pages of up to 22 rom files
 * allowing the user to select which rom file to start
 * Copy your *.gb rom files to the root directory of the SD card
 */
void rom_file_selector() {
    uint16_t num_page;
	char filename[22][256];
	uint16_t num_file;
	
	/* display the first page with up to 22 rom files */
	num_file=rom_file_selector_display_page(filename,num_page);

	/* select the first rom */
	uint8_t selected=0;
	st7789_text(filename[selected],0,selected*8,0xFFFF,0xF800);

	/* get user's input */
	bool up,down,left,right,a,b,select,start;
	while(true) {
		up=gpio_get(GPIO_UP);
		down=gpio_get(GPIO_DOWN);
		left=gpio_get(GPIO_LEFT);
		right=gpio_get(GPIO_RIGHT);
		a=gpio_get(GPIO_A);
		b=gpio_get(GPIO_B);
		select=gpio_get(GPIO_SELECT);
		start=gpio_get(GPIO_START);
		if(!start) {
			/* re-start the last game (no need to reprogram flash) */
			break;
		}
		if(!a | !b) {
			/* copy the rom from the SD card to flash and start the game */
			load_cart_rom_file(strcat("\\gb\\", filename[selected]));
			break;
		}
		if(!down) {
			/* select the next rom */
			st7789_text(filename[selected],0,selected*8,0xFFFF,0x0000);
			selected++;
			if(selected>=num_file) selected=0;
			st7789_text(filename[selected],0,selected*8,0xFFFF,0xF800);
			sleep_ms(150);
		}
		if(!up) {
			/* select the previous rom */
			st7789_text(filename[selected],0,selected*8,0xFFFF,0x0000);
			if(selected==0) {
				selected=num_file-1;
			} else {
				selected--;
			}
			st7789_text(filename[selected],0,selected*8,0xFFFF,0xF800);
			sleep_ms(150);
		}
		if(!right) {
			/* select the next page */
			num_page++;
			num_file=rom_file_selector_display_page(filename,num_page);
			if(num_file==0) {
				/* no files in this page, go to the previous page */
				num_page--;
				num_file=rom_file_selector_display_page(filename,num_page);
			}
			/* select the first file */
			selected=0;
			st7789_text(filename[selected],0,selected*8,0xFFFF,0xF800);
			sleep_ms(150);
		}
		if((!left) && num_page>0) {
			/* select the previous page */
			num_page--;
			num_file=rom_file_selector_display_page(filename,num_page);
			/* select the first file */
			selected=0;
			st7789_text(filename[selected],0,selected*8,0xFFFF,0xF800);
			sleep_ms(150);
		}
		tight_loop_contents();
	}
}

#endif

