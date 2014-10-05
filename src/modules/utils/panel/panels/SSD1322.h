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

class SSD1322: public LcdBase {
public:
	SSD1322();
	virtual ~SSD1322();

	void init();

	void home();
    void clear();
//    void display();
    void setCursor(uint8_t col, uint8_t row);

	void on_refresh(bool now=false);

	uint8_t readButtons();
	int readEncoderDelta();
	int getEncoderResolution() { return 1; }
	uint16_t get_screen_lines() { return 8; }
	bool hasGraphics() { return true; }

	void write(const char* line, int len);
    // blit a glyph of w pixels wide and h pixels high to x, y. offset pixel position in glyph by x_offset, y_offset.
    // span is the width in bytes of the src bitmap
    // The glyph bytes will be 8 bits of X pixels, msbit->lsbit from top left to bottom right
    void bltGlyph(int x, int y, int w, int h, const uint8_t *glyph, int span= 0, int x_offset=0, int y_offset=0);
    void renderGlyph(int x, int y, const uint8_t *g, int pixelWidth, int pixelHeight);
    void pixel(int x, int y, int colour);

    uint8_t getContrast() { return contrast; }
    void setContrast(uint8_t c);
	
private:
	int drawChar(int x, int y, unsigned char c, int color);
	void write_char(char value);

	void send_command(uint8_t cmd);
	void send_command(uint8_t cmd, uint8_t a);
	void send_command(uint8_t cmd, uint8_t a, uint8_t b);
	void send_data(const unsigned char* buf, size_t size);
	//send pic to whole screen
	void update();
	bool dirty;
	unsigned char *framebuffer;
	mbed::SPI* spi;
	Pin cs;
	Pin rst;
	Pin cd; // command/data selection
	Pin click_pin;
    Pin encoder_a_pin;
    Pin encoder_b_pin;

	// text cursor position
	uint8_t tx, ty;
    uint8_t contrast;
};

#endif /* SSD1322_H_ */
