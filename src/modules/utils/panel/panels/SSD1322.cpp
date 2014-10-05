/*
 * SSD1322.cpp
 *
 *  Created on: 21-06-2013
 *      Author: Wulfnor
 */

#include "SSD1322.h"
#include "font5x8.h"
#include "Kernel.h"
#include "platform_memory.h"
#include "Config.h"
#include "checksumm.h"
#include "StreamOutputPool.h"
#include "ConfigValue.h"



//definitions for lcd
#define LCDWIDTH 256
#define LCDHEIGHT 64
#define FB_SIZE LCDWIDTH*LCDHEIGHT/2
#define FONT_SIZE_X 6
#define FONT_SIZE_Y 8

#define panel_checksum             CHECKSUM("panel")
#define spi_channel_checksum       CHECKSUM("spi_channel")
#define spi_cs_pin_checksum        CHECKSUM("spi_cs_pin")
#define spi_frequency_checksum     CHECKSUM("spi_frequency")
#define encoder_a_pin_checksum     CHECKSUM("encoder_a_pin")
#define encoder_b_pin_checksum     CHECKSUM("encoder_b_pin")
#define click_button_pin_checksum  CHECKSUM("click_button_pin")
#define rst_pin_checksum           CHECKSUM("rst_pin")
#define cd_pin_checksum            CHECKSUM("cd_pin")

#define contrast_checksum          CHECKSUM("contrast")

#define CLAMP(x, low, high) { if ( (x) < (low) ) x = (low); if ( (x) > (high) ) x = (high); } while (0);
#define swap(a, b) { uint8_t t = a; a = b; b = t; }

SSD1322::SSD1322() :
	dirty(false)
{
    //SPI com
    // select which SPI channel to use
    int spi_channel = THEKERNEL->config->value(panel_checksum, spi_channel_checksum)->by_default(0)->as_number();
    PinName mosi, miso, sclk;
    if(spi_channel == 0) {
        mosi = P0_18; miso = P0_17; sclk = P0_15;
    } else if(spi_channel == 1) {
        mosi = P0_9; miso = P0_8; sclk = P0_7;
    } else {
        mosi = P0_18; miso = P0_17; sclk = P0_15;
    }

    this->spi = new mbed::SPI(mosi, miso, sclk);
    this->spi->frequency(THEKERNEL->config->value(panel_checksum, spi_frequency_checksum)->by_default(1000000)->as_number()); //4Mhz freq, can try go a little lower

    //chip select
    this->cs.from_string(THEKERNEL->config->value( panel_checksum, spi_cs_pin_checksum)->by_default("0.16")->as_string())->as_output();
    cs.set(1);

    //lcd reset
    this->rst.from_string(THEKERNEL->config->value( panel_checksum, rst_pin_checksum)->by_default("nc")->as_string())->as_output();
    if(this->rst.connected()) rst.set(1);

    //cd
    this->cd.from_string(THEKERNEL->config->value( panel_checksum, cd_pin_checksum)->by_default("2.13")->as_string())->as_output();
    cd.set(1);

    this->click_pin.from_string(THEKERNEL->config->value( panel_checksum, click_button_pin_checksum )->by_default("nc")->as_string())->as_input();
    this->encoder_a_pin.from_string(THEKERNEL->config->value( panel_checksum, encoder_a_pin_checksum)->by_default("nc")->as_string())->as_input();
    this->encoder_b_pin.from_string(THEKERNEL->config->value( panel_checksum, encoder_b_pin_checksum)->by_default("nc")->as_string())->as_input();

    // contrast override
    this->contrast = THEKERNEL->config->value(panel_checksum, contrast_checksum)->by_default(this->contrast)->as_number();

    framebuffer = (uint8_t *)AHB0.alloc(FB_SIZE); // grab some memory from USB_RAM
    if(framebuffer == NULL) {
        THEKERNEL->streams->printf("Not enough memory available for frame buffer");
    }

}

SSD1322::~SSD1322()
{
    delete this->spi;
    AHB0.dealloc(framebuffer);
}

//send commands to lcd
void SSD1322::send_command(uint8_t cmd)
{
    cs.set(0);
    cd.set(0);
	spi->write(cmd);
    cs.set(1);
}

void SSD1322::send_command(uint8_t cmd, uint8_t a)
{
    cs.set(0);
    cd.set(0);
	spi->write(cmd);
    cd.set(1);
	spi->write(a);
    cd.set(0);
    cs.set(1);
}

void SSD1322::send_command(uint8_t cmd, uint8_t a, uint8_t b)
{
    cs.set(0);
    cd.set(0);
	spi->write(cmd);
    cd.set(1);
	spi->write(a);
	spi->write(b);
    cd.set(0);
    cs.set(1);
}

//send data to lcd
void SSD1322::send_data(const unsigned char *buf, size_t size)
{
    cs.set(0);
    cd.set(1);
    while(size-- > 0) {
        spi->write(*buf++);
    }
    cd.set(0);
    cs.set(1);
}

//clearing screen
void SSD1322::clear()
{
    memset(framebuffer, 0, FB_SIZE);
    this->tx = 0;
    this->ty = 0;
	dirty = true;
}

void SSD1322::update()
{
	send_command(0x15, 0x1c, 0x5b); // set col addr
	send_command(0x75, 0, 0x3f); // set row addr
    send_command(0x5C);
    send_data(framebuffer, FB_SIZE);
}

void SSD1322::setCursor(uint8_t col, uint8_t row)
{
    this->tx = col * FONT_SIZE_X;
    this->ty = row * FONT_SIZE_Y;
}

void SSD1322::home()
{
    this->tx = 0;
    this->ty = 0;
}

void SSD1322::init()
{
    if(this->rst.connected()) {
		rst.set(1);
		wait_ms(1);
		rst.set(0);
		wait_ms(1);
		rst.set(1);
		wait_ms(1);
	}
	
	send_command(0xFD); //Set Command Lock
	send_command(0xFD, 0x12); /*SET COMMAND LOCK*/ 
	send_command(0xAE); /*DISPLAY OFF*/ 
	send_command(0xB3, 0x91);/*DISPLAYDIVIDE CLOCKRADIO/OSCILLATAR FREQUANCY*/ 
	send_command(0xCA, 0x3F); /*multiplex ratio, duty = 1/64*/ 
	send_command(0xA2, 0x00); /*set offset*/ 
	send_command(0xA1, 0x00); /*start line*/ 
	send_command(0xA0, 0x14, 0x11); /*set remap*/
	send_command(0xAB, 0x01); /*function selection external vdd */ 
	send_command(0xB4, 0xA0, 0xfd); 
	send_command(0xC1, contrast); 
	send_command(0xC7, 0x0f); /*master contrast current control*/ 
	send_command(0xB1, 0xE2); /*SET PHASE LENGTH*/
	send_command(0xD1, 0x82, 0x20); 
	send_command(0xBB, 0x1F); /*SET PRE-CHANGE VOLTAGE*/ 
	send_command(0xB6, 0x08); /*SET SECOND PRE-CHARGE PERIOD*/
	send_command(0xBE, 0x07); /* SET VCOMH */ 
	send_command(0xA6); /*normal display*/ 
	clear();
	update();
	send_command(0xAF); /*display ON*/
}

void SSD1322::setContrast(uint8_t c)
{
    contrast = c;
    send_command(0xc1, c);
}

void SSD1322::write_char(char c)
{
    if(c == '\n') {
        ty += FONT_SIZE_Y;
    }
    else if(c == '\r') {
        ty = 0;
    }
	else if(tx <= LCDWIDTH-FONT_SIZE_X) {
		dirty = true;
		for (uint8_t yi = 0; yi < 8; yi++ ) {
			int addr = (tx>>1) + (ty+yi)*(LCDWIDTH/2);
			uint8_t bits = font5x8[(c * 5) + yi];
			uint8_t a;
			a = (bits&0x80)>>3 | (bits&0x40)>>6;
            framebuffer[addr++] = a*0xf;
			a = (bits&0x20)>>1 | (bits&0x10)>>4;
            framebuffer[addr++] = a*0xf;
			a = (bits&0x08)<<1;
            framebuffer[addr++] = a*0xf;
        }
        tx += FONT_SIZE_X;
    }
}

void SSD1322::write(const char *line, int len)
{
    for (int i = 0; i < len; ++i) {
        write_char(line[i]);
    }
}

//refreshing screen
void SSD1322::on_refresh(bool now)
{
    if(dirty || now) {
		update();
		dirty = false;
    }
}

//reading button state
uint8_t SSD1322::readButtons(void)
{
    return (click_pin.get() ? BUTTON_SELECT : 0);
}

int SSD1322::readEncoderDelta()
{
    static int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    static uint8_t old_AB = 0;
    if(this->encoder_a_pin.connected()) {
        old_AB <<= 2;                   //remember previous state
        old_AB |= ( this->encoder_a_pin.get() + ( this->encoder_b_pin.get() * 2 ) );  //add current state
        return  enc_states[(old_AB & 0x0f)];

    } else {
        return 0;
    }
}

void SSD1322::bltGlyph(int x, int y, int w, int h, const uint8_t *glyph, int span, int x_offset, int y_offset)
{
    CLAMP(x, 0, LCDWIDTH - 1);
    CLAMP(y, 0, LCDHEIGHT - 1);
    CLAMP(w, 0, LCDWIDTH - x);
    CLAMP(h, 0, LCDHEIGHT - y);

    const uint8_t *src = &glyph[y_offset * span];

    for(int j = 0; j < h; j++) {
		for(int i = 0; i < w; i++) {
			uint8_t val = src[((x+i-x_offset) >> 3) + j * span];
            pixel(x + i, y + j, val & (128 >> (i & 0x7)));
        }
    }
	dirty = true;
}

void SSD1322::pixel(int x, int y, int colour)
{
    unsigned char *byte = framebuffer + y * (LCDWIDTH>>1) + (x>>1);
	if((x&1)==0) {
		*byte = (*byte&0x0f)|(colour<<4);
	} else {
		*byte = (*byte&0xf0)|colour;
	}
}
