/*
 * SSD1322.h
 *
 *  Created on: 04-10-2014
 *      Author: Hakan Langemark
 */

#ifndef SSD1322_H_
#define SSD1322_H_

#include "LcdBase.h"
#include "mbed.h"
#include "libs/Pin.h"
#include <initializer_list>
#include <memory>

class SSD1322: public LcdBase {
public:
	SSD1322();
	virtual ~SSD1322();

	void init() override;

	void home() override;
    void clear() override;
    void setCursor(uint8_t col, uint8_t row) override;

	void on_refresh(bool now=false) override;

	uint8_t readButtons() override;
	int readEncoderDelta() override;
	int getEncoderResolution() override { return 1; }
	uint16_t get_screen_lines() override { return 8; }
	bool hasGraphics() override { return true; }

	void write(const char* line, int len) override;
    // blit a glyph of w pixels wide and h pixels high to x, y. offset pixel position in glyph by x_offset, y_offset.
    // span is the width in bytes of the src bitmap
    // The glyph bytes will be 8 bits of X pixels, msbit->lsbit from top left to bottom right
    void bltGlyph(int x, int y, int w, int h, const uint8_t *glyph, int span= 0, int x_offset=0, int y_offset=0) override;

    uint8_t getContrast() override { return contrast_; }
    void setContrast(uint8_t c) override;
	
private:
    void pixel(int x, int y, uint8_t colour);
	void write_char(char value);

	void send_command(uint8_t cmd, std::initializer_list<uint8_t> data = {});
	void send_data(const unsigned char* buf, size_t size);
	//send pic to whole screen
	void update();
    
	bool dirty_;
	uint8_t *framebuffer_;
	std::unique_ptr<mbed::SPI> spi_;
	Pin cs_;
	Pin rst_;
	Pin cd_; // command/data selection
	Pin click_pin_;
    Pin encoder_a_pin_;
    Pin encoder_b_pin_;

	// text cursor position
	uint8_t tx_, ty_;
    uint8_t contrast_;
    uint8_t old_AB_;
};

#endif /* SSD1322_H_ */
