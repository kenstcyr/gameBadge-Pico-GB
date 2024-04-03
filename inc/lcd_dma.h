#ifndef LCD_DMA_H
#define LCD_DMA_H

#define LCD_WIDTH 160
#define LCD_PALETTE_ALL 0x30

// Includes
#include <hardware/dma.h>
#include <hardware/spi.h>
#include "pico_ST7789.h"
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Variables
static int lcdState = 0;                       // Keeps track of which state the LCD is in
static bool localFrameDrawFlag = false;        // Flag to indicate that a frame can be drawn
static int lastDMA = 5;                        // Used to round robin the DMA channels
static bool isRendering = false;               // Render status flag 0 = null, true = render in progress, false = render complete
static int whichBuffer = 0;                    // We have 2 row buffers. We draw one, send via DMA, and draw next while DMA is running
static uint16_t rowBuffer[2][240];      // 2 row buffers, 240 pixels wide
static uint16_t linebuffer[2][240];     // Sets up 2 buffers of 240 pixels
static uint8_t renderRow = 0;                  // Keeps track of which rows have been rendered

// Function prototypes
void dmaLCD(int whatChannel, const void* data, int whatSize);
void LCDlogic();
void lcdRenderRow();
void UpdateBuffer(uint8_t pixelData[LCD_WIDTH], uint16_t palette[3][4]);

#ifdef __cplusplus
}
#endif

#endif