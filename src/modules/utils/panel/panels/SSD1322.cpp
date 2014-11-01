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

template<typename Val>
Val clamp(Val x, Val min, Val max)
{
  if( x < min ) return min;
  else if( x > max ) return max;
  else return x;
}

SSD1322::SSD1322() :
	dirty_{false},
    framebuffer_{nullptr},
    contrast_{127},
    old_AB_{0}
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

    spi_ = std::unique_ptr<mbed::SPI>(new mbed::SPI(mosi, miso, sclk));
    spi_->frequency(THEKERNEL->config->value( panel_checksum, spi_frequency_checksum)->by_default(1000000)->as_number());

    //chip select
    cs_.from_string(THEKERNEL->config->value( panel_checksum, spi_cs_pin_checksum)->by_default("1.30")->as_string())->as_output();
    cs_.set(1);

    //lcd reset
    rst_.from_string(THEKERNEL->config->value( panel_checksum, rst_pin_checksum)->by_default("nc")->as_string())->as_output();
    if(rst_.connected()) rst_.set(1);

    //cd
    cd_.from_string(THEKERNEL->config->value( panel_checksum, cd_pin_checksum)->by_default("1.31")->as_string())->as_output();
    cd_.set(1);

    click_pin_.from_string(THEKERNEL->config->value( panel_checksum, click_button_pin_checksum )->by_default("nc")->as_string())->as_input();
    encoder_a_pin_.from_string(THEKERNEL->config->value( panel_checksum, encoder_a_pin_checksum)->by_default("nc")->as_string())->as_input();
    encoder_b_pin_.from_string(THEKERNEL->config->value( panel_checksum, encoder_b_pin_checksum)->by_default("nc")->as_string())->as_input();

    // contrast override
    contrast_ = THEKERNEL->config->value(panel_checksum, contrast_checksum)->by_default(contrast_)->as_number();

    framebuffer_ = (uint8_t *)AHB0.alloc(FB_SIZE); // grab some memory from USB_RAM
    if(framebuffer_ == NULL) {
        THEKERNEL->streams->printf("Not enough memory available for frame buffer");
    }

}

SSD1322::~SSD1322()
{
    AHB0.dealloc(framebuffer_);
}

void SSD1322::send_command(uint8_t cmd, std::initializer_list<uint8_t> data)
{
    cs_.set(0);
    cd_.set(0);
	spi_->write(cmd);
    if(data.size()>0) {
      cd_.set(1);
      for(auto val : data) spi_->write(val);
      cd_.set(0);
    }
    cs_.set(1);
}

void SSD1322::send_data(const unsigned char *buf, size_t size)
{
    cs_.set(0);
    cd_.set(1);
    while(size-- > 0) {
        spi_->write(*buf++);
    }
    cd_.set(0);
    cs_.set(1);
}

//clearing screen
void SSD1322::clear()
{
    memset(framebuffer_, 0, FB_SIZE);
    tx_ = 0;
    ty_ = 0;
	dirty_ = true;
}

void SSD1322::update()
{
    // Note: Hardcoded for 256x64
	send_command(0x15, {0x1c, 0x5b}); // set col addr
	send_command(0x75, {0, 0x3f}); // set row addr
    send_command(0x5C);
    send_data(framebuffer_, FB_SIZE);
}

void SSD1322::setCursor(uint8_t col, uint8_t row)
{
    tx_ = col * FONT_SIZE_X;
    ty_ = row * FONT_SIZE_Y;
}

void SSD1322::home()
{
    tx_ = 0;
    ty_ = 0;
}

void SSD1322::init()
{
    if(rst_.connected()) {
		rst_.set(1);
		wait_ms(1);
		rst_.set(0);
		wait_ms(1);
		rst_.set(1);
		wait_ms(1);
	}
	
	send_command(0xFD); //Set Command Lock
	send_command(0xFD, {0x12}); /*SET COMMAND LOCK*/ 
	send_command(0xAE); /*DISPLAY OFF*/ 
	send_command(0xB3, {0x91});/*DISPLAYDIVIDE CLOCKRADIO/OSCILLATOR FREQUENCY*/ 
	send_command(0xCA, {0x3F}); /*multiplex ratio, duty = 1/64*/ 
	send_command(0xA2, {0x00}); /*set offset*/ 
	send_command(0xA1, {0x00}); /*start line*/ 
	send_command(0xA0, {0x14, 0x11}); /*set remap*/
	send_command(0xAB, {0x01}); /*function selection external vdd */ 
	send_command(0xB4, {0xA0, 0xfd}); 
	send_command(0xC1, {contrast_}); 
	send_command(0xC7, {0x0f}); /*master contrast current control*/ 
	send_command(0xB1, {0xE2}); /*SET PHASE LENGTH*/
	send_command(0xD1, {0x82, 0x20}); 
	send_command(0xBB, {0x1F}); /*SET PRE-CHANGE VOLTAGE*/ 
	send_command(0xB6, {0x08}); /*SET SECOND PRE-CHARGE PERIOD*/
	send_command(0xBE, {0x07}); /* SET VCOMH */ 
	send_command(0xA6); /*normal display*/ 
	clear();
	update();
	send_command(0xAF); /*display ON*/
}

void SSD1322::setContrast(uint8_t c)
{
    contrast_ = c;
    send_command(0xc1, {c});
}

void SSD1322::write_char(char c)
{
    if(c == '\n') {
        ty_ += FONT_SIZE_Y;
    }
    else if(c == '\r') {
        ty_ = 0;
    }
	else if(tx_ <= LCDWIDTH-FONT_SIZE_X) {
		dirty_ = true;
		for (uint8_t yi = 0u; yi < 8u; yi++ ) {
			uint8_t *addr = framebuffer_ + (tx_>>1) + (ty_+yi)*(LCDWIDTH/2);
			uint8_t bits = font5x8[(c * 5) + yi];
			uint8_t a;
			a = (bits&0x80)>>3 | (bits&0x40)>>6;
            *addr++ = a*0xf;
			a = (bits&0x20)>>1 | (bits&0x10)>>4;
            *addr++ = a*0xf;
			a = (bits&0x08)<<1;
            *addr++ = a*0xf;
        }
        tx_ += FONT_SIZE_X;
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
    if(dirty_ || now) {
		update();
		dirty_ = false;
    }
}

//reading button state
uint8_t SSD1322::readButtons(void)
{
    return (click_pin_.get() ? BUTTON_SELECT : 0);
}

int SSD1322::readEncoderDelta()
{
    static const int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    if(encoder_a_pin_.connected()) {
        old_AB_ <<= 2;                   //remember previous state
        old_AB_ |= ( encoder_a_pin_.get() + ( encoder_b_pin_.get() * 2 ) );  //add current state
        return enc_states[(old_AB_ & 0x0f)];

    } else {
        return 0;
    }
}

void SSD1322::bltGlyph(int x, int y, int w, int h, const uint8_t *glyph, int span, int x_offset, int y_offset)
{
    x = clamp(x, 0, LCDWIDTH - 1);
    y = clamp(y, 0, LCDHEIGHT - 1);
    w = clamp(w, 0, LCDWIDTH - x);
    h = clamp(h, 0, LCDHEIGHT - y);

    const uint8_t *src_begin = glyph + y_offset * span + (x_offset >> 3);
    const uint8_t mask_begin = 128 >> (x_offset & 7);
    for(int yi = y; yi < y + h; ++yi) {
        uint8_t mask = mask_begin;
        const uint8_t *src = src_begin;
		for(uint8_t xi = x; xi < x + w; ++xi) {
            pixel(xi, yi, (*src) & mask ? 0xf : 0);
            if(mask == 0) {
                mask = 128;
                ++src;
            }
            else {
                mask >>= 1;
            }
        }
        src_begin += span;
    }
	dirty_ = true;
}

void SSD1322::pixel(int x, int y, uint8_t colour)
{
    uint8_t *byte = framebuffer_ + y * (LCDWIDTH >> 1) + (x >> 1);
	if((x&1)==0) {
		*byte = (*byte&0x0f)|(colour<<4);
	} else {
		*byte = (*byte&0xf0)|colour;
	}
}
