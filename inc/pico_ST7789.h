//ST7789 driver for Pi Pico based Gamebadge3
//Uses this library mostly for setup then dumps data via DMA

#ifndef pico_ST7789_h
#define pico_ST7789_h

#include "hardware/spi.h"

	struct st7789_config {
		spi_inst_t* spi;
		uint gpio_din;
		uint gpio_clk;
		int gpio_cs;
		uint gpio_dc;
		uint gpio_rst;
		uint gpio_bl;
	};
	
	//struct st7789_config st7789_cfg;

	void st7789_init(const struct st7789_config* config, uint16_t width, uint16_t height);
	void st7789_backlight(bool state);
	void st7789_cmd(uint8_t cmd, const uint8_t* data, size_t len);	
	void st7789_cmd_1(uint8_t cmd, uint8_t param);
	void st7789_ramwr();
	void st7789_set_cursor(uint16_t x, uint16_t y);
	void st7789_caset(uint16_t xs, uint16_t xe);
	void st7789_raset(uint16_t ys, uint16_t ye);
	void st7789_setRotation(uint8_t which);
	void st7789_setAddressWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

	// KAS - Added to support peanutGB
	void st7789_set_x(uint16_t x);
	void st7789_write_pixels(const uint16_t *halfwords, size_t len);
	void st7789_fill(uint16_t color);
	void st7789_drawPixel(uint16_t x, uint16_t y, uint16_t color);
	void st7789_fillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color);
	void st7789_get_letter(uint16_t *fbuf,char l,uint16_t color,uint16_t bgcolor);
	void st7789_text(char *s,uint8_t x,uint8_t y,uint16_t color,uint16_t bgcolor);
	void st7789_blit(uint16_t *fbuf,uint8_t x,uint8_t y,uint8_t w,uint8_t h);
	//uint16_t st7789_width;
	//uint16_t st7789_height;
	//bool st7789_data_mode = false;

#endif

