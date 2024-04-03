#include "lcd_dma.h"

// Sends data to the ST7789 one row at a time. As the previous row is DMA'd to LCD the next row is drawn
void dmaLCD(int whatChannel, const void* data, int whatSize) {

    while(dma_channel_is_busy(whatChannel) == true) {}                      // Wait for the channel to be free

    dma_channel_unclaim(whatChannel);                                       // Unclaim the channel
    dma_channel_config c = dma_channel_get_default_config(whatChannel);     // Get the default config for the channel
	channel_config_set_high_priority(&c, true);                             // Set high priority
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);                 // Set the transfer data size to 16 bits
    channel_config_set_dreq(&c, spi_get_dreq(spi0, true));                  
    
    dma_channel_configure(whatChannel, &c,
                          &spi_get_hw(spi0)->dr,                            // write address is SPI bus
                          data,                                             // read address is incoming data
                          whatSize,                                         // element count (each element is of size transfer_data_size)
                          false);                                           // don't start yet

    dma_start_channel_mask((1u << whatChannel));                            // Start the DMA channel 
}

// Logic for implementing the LCD DMA
void LCDlogic() 
{
	switch(lcdState) {
	
		case 0:										                        // Waiting for frame draw flag
			if (localFrameDrawFlag == true) {		                        // Flag set?
				localFrameDrawFlag = false;			                        // Always respond by clearing flag (even if we don't draw a new one because paused)			
				lcdState = 1;   					                        // Advance to next state	
			}
		break;
		
		case 1:							                                    // Start LCD frame
        printf("State 1\n");
			if (dma_channel_is_busy(lastDMA) == false) {		            // If DMA is running, let it finish
				isRendering = true;                                         // Set rendering flag
				st7789_setAddressWindow(0, 0, 240, 240);	                // Set entire LCD for drawing
				st7789_ramwr();                       		                // Switch to RAM writing mode	
				lcdState = 2;                                               // Advance to next state
			}
		break;
	
		case 2:											                    
			lcdRenderRow();                                                 // Render 1 row
			lcdState = 3;								                    
		break;
		
		case 3:
			if (dma_channel_is_busy(lastDMA) == false) {		            // DMA done sending previous row?

				if (++lastDMA > 6) {							            // Toggle channels 5 and 6. Using same channel in series causes issues, not sure why? (something not cleared in time?)
					lastDMA = 5;
				}
		
				dmaLCD(lastDMA, &linebuffer[whichBuffer][0], 240);			// Send the 240x16 pixels we just built to the LCD    

				if (++whichBuffer > 1) {							        // Switch up buffers, we will draw the next while the prev is being DMA'd to LCD
					whichBuffer = 0;
				}				
				
				if (++renderRow == 240) {				                    // LCD done?
					isRendering = false;					                // Render complete, wait for next draw flag (this keeps Core0 from drawing in screen
					lcdState = 0;	
				}
				else {
					lcdState = 2;						                    // Not done? Render next row and wait for DMA
				}
				
			}
		break;		
	}
}

void lcdRenderRow()
{
    memcpy(&linebuffer[whichBuffer][0], &rowBuffer[0], 240);			// Copy the row buffer to the line buffer
}

void UpdateBuffer(uint8_t pixelData[LCD_WIDTH], uint16_t palette[3][4])
{
    static uint16_t fb[LCD_WIDTH];										// 16-bit frame buffer
	memset(rowBuffer, 0, sizeof(rowBuffer));				            // Clear the row buffer

	for(unsigned int x = 0; x < LCD_WIDTH; x++)
	{
		fb[x] = palette[(pixelData[x] & LCD_PALETTE_ALL) >> 4]
				[pixelData[x] & 3];

        rowBuffer[whichBuffer][x+40] = fb[x];								    // Fill the scaled buffer with an offset
	}
}