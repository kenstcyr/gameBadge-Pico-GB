//ST7789 driver for Pi Pico based Gamebadge3
//Uses this library mostly for setup then dumps data via DMA

#include "pico_ST7789.h"

#include <string.h>
#include "hardware/gpio.h"
#include "pico/time.h"

struct st7789_config st7789_cfg;
uint16_t st7789_width;
uint16_t st7789_height;
bool st7789_data_mode = false;

#define ST77XX_MADCTL_MY 0x80
#define ST77XX_MADCTL_MX 0x40

#define ST77XX_MADCTL_MV 0x20
#define ST77XX_MADCTL_ML 0x10

#define ST77XX_MADCTL_RGB 0x00

uint8_t _xstart;
uint8_t _ystart;

void st7789_init(const struct st7789_config* config, uint16_t width, uint16_t height)
{
    memcpy(&st7789_cfg, config, sizeof(st7789_cfg));				//Copy referenced structure to our own copy
    st7789_width = width;
    st7789_height = height;

    //spi_init(st7789_cfg.spi, 125 * 1000 * 1000);					//Set SPI to max speed, CPU must be 125Hz
	
    if (st7789_cfg.gpio_cs > -1) {
        spi_set_format(st7789_cfg.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    } else {
        spi_set_format(st7789_cfg.spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    }

    gpio_set_function(st7789_cfg.gpio_din, GPIO_FUNC_SPI);
    gpio_set_function(st7789_cfg.gpio_clk, GPIO_FUNC_SPI);

    if (st7789_cfg.gpio_cs > -1) {
        gpio_init(st7789_cfg.gpio_cs);
    }
    gpio_init(st7789_cfg.gpio_dc);
    gpio_init(st7789_cfg.gpio_rst);
    gpio_init(st7789_cfg.gpio_bl);

    if (st7789_cfg.gpio_cs > -1) {
        gpio_set_dir(st7789_cfg.gpio_cs, GPIO_OUT);
    }
    gpio_set_dir(st7789_cfg.gpio_dc, GPIO_OUT);
    gpio_set_dir(st7789_cfg.gpio_rst, GPIO_OUT);
    gpio_set_dir(st7789_cfg.gpio_bl, GPIO_OUT);

	gpio_put(st7789_cfg.gpio_bl, 1);

    if (st7789_cfg.gpio_cs > -1) {
        gpio_put(st7789_cfg.gpio_cs, 1);
    }
    gpio_put(st7789_cfg.gpio_dc, 1);
	gpio_put(st7789_cfg.gpio_rst, 0);
	sleep_ms(150);
    gpio_put(st7789_cfg.gpio_rst, 1);
    sleep_ms(100);
    
    // SWRESET (01h): Software Reset
    st7789_cmd(0x01, NULL, 0);
    sleep_ms(150);

    // SLPOUT (11h): Sleep Out
    st7789_cmd(0x11, NULL, 0);
    sleep_ms(10);

	st7789_setRotation(1);

    // COLMOD (3Ah): Interface Pixel Format
    // - RGB interface color format     = 65K of RGB interface
    // - Control interface color format = 16bit/pixel

    //uint8_t cmd = 0x2c;
    //spi_write_blocking(st7789_cfg.spi, &cmd, sizeof(cmd));
    
    uint8_t cmd = 0x55;
    st7789_cmd(0x3a, &cmd, 1);
    sleep_ms(10);

    st7789_caset(0, width - 1);
    st7789_raset(0, height - 1);
	
    // INVON (21h): Display Inversion On
    st7789_cmd(0x21, NULL, 0);
    sleep_ms(10);

    // NORON (13h): Normal Display Mode On
    st7789_cmd(0x13, NULL, 0);
    sleep_ms(10);

    // DISPON (29h): Display On
    st7789_cmd(0x29, NULL, 0);
    sleep_ms(10);
	 
}

void st7789_backlight(bool state) {

	if (state == true) {
		gpio_put(st7789_cfg.gpio_bl, 1);
	}
	else {
		gpio_put(st7789_cfg.gpio_bl, 0);
	}
	
}

void st7789_cmd(uint8_t cmd, const uint8_t* data, size_t len)
{
    if (st7789_cfg.gpio_cs > -1) {
        spi_set_format(st7789_cfg.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    } else {
        spi_set_format(st7789_cfg.spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    }
    st7789_data_mode = false;

    sleep_us(1);
    if (st7789_cfg.gpio_cs > -1) {
        gpio_put(st7789_cfg.gpio_cs, 0);
    }
    gpio_put(st7789_cfg.gpio_dc, 0);
    sleep_us(1);
    
    spi_write_blocking(st7789_cfg.spi, &cmd, sizeof(cmd));
    
    if (len) {
        sleep_us(1);
        gpio_put(st7789_cfg.gpio_dc, 1);
        sleep_us(1);
        
        spi_write_blocking(st7789_cfg.spi, data, len);
    }

    sleep_us(1);
    if (st7789_cfg.gpio_cs > -1) {
        gpio_put(st7789_cfg.gpio_cs, 1);
    }
    gpio_put(st7789_cfg.gpio_dc, 1);
    sleep_us(1);
}

void st7789_caset(uint16_t xs, uint16_t xe)
{
	uint8_t data[4];
	
	data[0] = xs >> 8;
	data[1] = xs & 0xFF;
	data[2] = xe >> 8;
	data[3] = xe & 0xFF;
	
    st7789_cmd(0x2a, data, sizeof(data));			// CASET (2Ah): Column Address Set
}

void st7789_raset(uint16_t ys, uint16_t ye)
{
	uint8_t data[4];
	
	data[0] = ys >> 8;
	data[1] = ys & 0xFF;
	data[2] = ye >> 8;
	data[3] = ye & 0xFF;

    st7789_cmd(0x2b, data, sizeof(data));			// RASET (2Bh): Row Address Set
}


// Puts the ST7789 into write mode. SPI commands following this should be the pixel data
void st7789_ramwr()
{
    sleep_us(1);
    if (st7789_cfg.gpio_cs > -1) {
        gpio_put(st7789_cfg.gpio_cs, 0);
    }
    gpio_put(st7789_cfg.gpio_dc, 0);
    sleep_us(1);

    // RAMWR (2Ch): Memory Write
    uint8_t cmd = 0x2c;
    spi_write_blocking(st7789_cfg.spi, &cmd, sizeof(cmd));

    sleep_us(1);

	if (st7789_cfg.gpio_cs > -1) {
		gpio_put(st7789_cfg.gpio_cs, 0);
		spi_set_format(st7789_cfg.spi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	} else {
		spi_set_format(st7789_cfg.spi, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
	}	

    gpio_put(st7789_cfg.gpio_dc, 1);
    sleep_us(1);
}

void st7789_write_pixels(const uint16_t *halfwords, size_t len)
{
    spi_write16_blocking(st7789_cfg.spi, halfwords, len);
}

void st7789_set_cursor(uint16_t x, uint16_t y)
{
    st7789_caset(x, st7789_width - 1);
    st7789_raset(y, st7789_height - 1);
}

void st7789_set_x(uint16_t x)
{
    st7789_caset(x, st7789_width - 1);
}

void st7789_setRotation(uint8_t which) {
	
  uint8_t cmd = 0;

  switch (which & 0x03) {
	  case 0:
		cmd = ST77XX_MADCTL_MX | ST77XX_MADCTL_MY | ST77XX_MADCTL_RGB;
		_xstart = 0;
		_ystart = 80;
		break;
	  case 1:
		cmd = ST77XX_MADCTL_MY | ST77XX_MADCTL_MV | ST77XX_MADCTL_RGB;
		_xstart = 80;
		_ystart = 0;
		break;
	  case 2:
		cmd = ST77XX_MADCTL_RGB;
		_xstart = 0;
		_ystart = 0;
		break;
	  case 3:
		cmd = ST77XX_MADCTL_MX | ST77XX_MADCTL_MV | ST77XX_MADCTL_RGB;
		_xstart = 0;
		_ystart = 0;
		break;
  }

  st7789_cmd(0x36, &cmd, 1);
	
}

void st7789_setAddressWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
	
	x += _xstart;
	y += _ystart;
	//uint32_t xa = ((uint32_t)x << 16) | (x + w - 1);
	//uint32_t ya = ((uint32_t)y << 16) | (y + h - 1);

	st7789_caset(x, x + (w - 1));		 // Column addr set
	st7789_raset(y, y + (h - 1)); 		// Row addr set

}

// Fill the entire display with the given 16-bit color.
void st7789_fill(uint16_t color) {
    
    st7789_setAddressWindow(0, 0, st7789_width, st7789_height);
    st7789_ramwr();
    
    for(uint16_t i=0;i<st7789_width*st7789_height;i++) {
    	spi_write16_blocking(st7789_cfg.spi, &color, 1);
    }
}

void st7789_fillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color)
{
    st7789_setAddressWindow(x, y, w, h);
    st7789_ramwr();
    
    for(uint16_t i=0;i<(w-x)*(h-y);i++) {
    	spi_write16_blocking(st7789_cfg.spi, &color, 1);
    }
}

void st7789_drawPixel(uint16_t x, uint16_t y, uint16_t color) {
     //st7789_setAddressWindow(x, y, 1, 1);
     //st7789_ramwr();
     spi_write16_blocking(st7789_cfg.spi, &color, 1);
     
}

void st7789_text(char *s,uint8_t x,uint8_t y,uint16_t color,uint16_t bgcolor) {
	uint16_t fbuf[8*8];
	for(uint8_t i=0;i<strlen(s);i++) {
		st7789_get_letter(fbuf,s[i],color,bgcolor);
		st7789_blit(fbuf,x,y,8,8);
		x+=8;
		if(x>27*8) {
			break;
		}
	}
}

void st7789_blit(uint16_t *fbuf,uint8_t x,uint8_t y,uint8_t w,uint8_t h) {
    st7789_setAddressWindow(x,y,w,h);
    st7789_ramwr();
    st7789_write_pixels(fbuf,w*h);
}

void st7789_get_letter(uint16_t *fbuf,char l,uint16_t color,uint16_t bgcolor) {
	uint8_t letter[8];
	uint8_t row;
	
	switch(l)
	{
		case 'a':
		case 'A':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
						              0b01111110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case 'b':
		case 'B':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'c':
		case 'C':
		{
			const uint8_t letter_[8]={0b00011110,
						              0b00110000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00110000,
						              0b00011110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'd':
		case 'D':
		{
			const uint8_t letter_[8]={0b01111000,
						              0b01101100,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01101100,
						              0b01111000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'e':
		case 'E':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01100000,
						              0b01111000,
						              0b01100000,
						              0b01100000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'f':
		case 'F':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01100000,
						              0b01111000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'g':
		case 'G':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100000,
						              0b01101110,
						              0b01100110,
						              0b01100110,
						              0b00111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'h':
		case 'H':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01111110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'i':
		case 'I':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'j':
		case 'J':
		{
			const uint8_t letter_[8]={0b00000110,
						              0b00000110,
						              0b00000110,
						              0b00000110,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'k':
		case 'K':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11001100,
						              0b11011000,
						              0b11110000,
						              0b11011000,
						              0b11001100,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'l':
		case 'L':
		{
			const uint8_t letter_[8]={0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'm':
		case 'M':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11101110,
						              0b11111110,
						              0b11010110,
						              0b11000110,
						              0b11000110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'n':
		case 'N':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11100110,
						              0b11110110,
						              0b11011110,
						              0b11001110,
						              0b11000110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'o':
		case 'O':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'p':
		case 'P':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'q':
		case 'Q':
		{
			const uint8_t letter_[8]={0b01111000,
						              0b11001100,
						              0b11001100,
						              0b11001100,
						              0b11001100,
						              0b11011100,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'r':
		case 'R':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01101100,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 's':
		case 'S':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01110000,
						              0b00111100,
						              0b00001110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 't':
		case 'T':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'u':
		case 'U':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'v':
		case 'V':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00111100,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'w':
		case 'W':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11000110,
						              0b11000110,
						              0b11010110,
						              0b11111110,
						              0b11101110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'x':
		case 'X':
		{
			const uint8_t letter_[8]={0b11000011,
						              0b01100110,
						              0b00111100,
						              0b00011000,
						              0b00111100,
						              0b01100110,
						              0b11000011,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'y':
		case 'Y':
		{
			const uint8_t letter_[8]={0b11000011,
						              0b01100110,
						              0b00111100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'z':
		case 'Z':
		{
			const uint8_t letter_[8]={0b11111110,
						              0b00001100,
						              0b00011000,
						              0b00110000,
						              0b01100000,
						              0b11000000,
						              0b11111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case '-':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b01111110,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case '(':
		case '[':
		case '{':
		{
			const uint8_t letter_[8]={0b00001100,
						              0b00011000,
						              0b00110000,
									  0b00110000,
						              0b00110000,
						              0b00011000,
						              0b00001100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case ')':
		case ']':
		case '}':
		{
			const uint8_t letter_[8]={0b00110000,
						              0b00011000,
						              0b00001100,
									  0b00001100,
						              0b00001100,
						              0b00011000,
						              0b00110000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case ',':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b00000000,
						              0b00000000,
						              0b00011000,
						              0b00011000,
						              0b00110000};
			memcpy(letter,letter_,8);
			break;
		}

		case '.':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b00000000,
						              0b00000000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '!':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00011000,
						              0b00011000,
									  0b00011000,
						              0b00011000,
						              0b00000000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '&':
		{
			const uint8_t letter_[8]={0b00111000,
						              0b01101100,
						              0b01101000,
									  0b01110110,
						              0b11011100,
						              0b11001110,
						              0b01111011,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '\'':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00011000,
						              0b00110000,
									  0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '0':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01101110,
									  0b01111110,
						              0b01110110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '1':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00111000,
						              0b01111000,
									  0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '2':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b00000110,
									  0b00001100,
						              0b00011000,
						              0b00110000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '3':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b00000110,
									  0b00011100,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '4':
		{
			const uint8_t letter_[8]={0b00011100,
						              0b00111100,
						              0b01101100,
									  0b11001100,
						              0b11111110,
						              0b00001100,
						              0b00001100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '5':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01111100,
									  0b00000110,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '6':
		{
			const uint8_t letter_[8]={0b00011100,
						              0b00110000,
						              0b01100000,
									  0b01111100,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '7':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b00000110,
						              0b00000110,
									  0b00001100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '8':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
									  0b00111100,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '9':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
									  0b00111110,
						              0b00000110,
						              0b00001100,
						              0b00111000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		default:
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}
	}

	for(uint8_t y=0;y<8;y++) {
		row=letter[y];
		for(uint8_t x=0;x<8;x++) {
			if(row & 128) {
				fbuf[y*8+x]=color;
			} else {
				fbuf[y*8+x]=bgcolor;
			}
			row=row<<1;
		}
	}
}