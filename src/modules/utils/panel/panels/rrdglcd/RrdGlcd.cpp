#include "RrdGlcd.h"

#include "platform_memory.h"
#include "StreamOutputPool.h"
#include "../font5x8.h"

#define ST7920_CS()              {cs.set(1);wait_us(10);}
#define ST7920_NCS()             {cs.set(0);wait_us(10);}
#define ST7920_WRITE_BYTE(a)     {this->spi->write((a)&0xf0);this->spi->write((a)<<4);wait_us(10);}
#define ST7920_WRITE_BYTES(p,l)  {uint8_t i;for(i=0;i<l;i++){this->spi->write(*p&0xf0);this->spi->write(*p<<4);p++;} wait_us(10); }
#define ST7920_SET_CMD()         {this->spi->write(0xf8);wait_us(10);}
#define ST7920_SET_DAT()         {this->spi->write(0xfa);wait_us(10);}
#define PAGE_HEIGHT 32  //512 byte framebuffer
#define WIDTH 128
#define HEIGHT 64
#define FB_SIZE WIDTH*HEIGHT/8

RrdGlcd::RrdGlcd(int spi_channel, Pin cs) {
    PinName mosi, miso, sclk;
    if(spi_channel == 0) {
        mosi = P0_18; miso = P0_17; sclk = P0_15;
    } else if(spi_channel == 1) {
        mosi = P0_9; miso = P0_8; sclk = P0_7;
    } else {
        mosi = P0_18; miso = P0_17; sclk = P0_15;
    }

    this->spi = new mbed::SPI(mosi, miso, sclk);

    //chip select
    this->cs= cs;
    this->cs.set(0);
    fb= (uint8_t *)AHB0.alloc(FB_SIZE); // grab some memoery from USB_RAM
    if(fb == NULL) {
        THEKERNEL->streams->printf("Not enough memory available for frame buffer");
    }
    inited= false;
    dirty= false;
}

RrdGlcd::~RrdGlcd() {
    delete this->spi;
    AHB0.dealloc(fb);
}

void RrdGlcd::setFrequency(int freq) {
       this->spi->frequency(freq);
}

void RrdGlcd::initDisplay() {
    if(fb == NULL) return;
    ST7920_CS();
    clearScreen();  // clear framebuffer
    wait_ms(90);                 //initial delay for boot up
    ST7920_SET_CMD();
    ST7920_WRITE_BYTE(0x08);       //display off, cursor+blink off
    ST7920_WRITE_BYTE(0x01);       //clear CGRAM ram
    wait_ms(10);                 //delay for cgram clear
    ST7920_WRITE_BYTE(0x3E);       //extended mode + gdram active
    for(int y=0;y<HEIGHT/2;y++)        //clear GDRAM
    {
        ST7920_WRITE_BYTE(0x80|y);   //set y
        ST7920_WRITE_BYTE(0x80);     //set x = 0
        ST7920_SET_DAT();
        for(int i=0;i<2*WIDTH/8;i++)     //2x width clears both segments
            ST7920_WRITE_BYTE(0);
        ST7920_SET_CMD();
    }
    ST7920_WRITE_BYTE(0x0C); //display on, cursor+blink off
    ST7920_NCS();
    inited= true;
}

void RrdGlcd::clearScreen() {
    if(fb == NULL) return;
    memset(this->fb, 0, FB_SIZE);
    dirty= true;
}

// render into local screenbuffer
void RrdGlcd::displayString(int row, int col, const char *ptr, int length) {
    for (int i = 0; i < length; ++i) {
        displayChar(row, col, ptr[i]);
        col+=1;
    }
    dirty= true;
}

void RrdGlcd::renderChar(uint8_t *fb, char c, int ox, int oy) {
    if(fb == NULL) return;
    // using the specific font data where x is in one byte and y is in consecutive bytes
    // the x bits are left aligned and right padded
    int i= c*8; // character offset in font array
    int o= ox%8; // where in fb byte does it go
    int a= oy*16 + ox/8; // start address in frame buffer
    int mask= ~0xF8 >> o; // mask off top bits
    int mask2= ~0xF8 << (8-o); // mask off bottom bits
    for(int y=0;y<8;y++) {
        int b= font5x8[i+y]; // get font byte
        fb[a] &= mask; // clear top bits for font
        fb[a] |= (b>>o); // or in the fonts 1 bits
        if(o >= 4) { // it spans two fb bytes
            fb[a+1] &= mask2; // clear bottom bits for font
            fb[a+1] |= (b<<(8-o)); // or in the fonts 1 bits
        }
        a+=16; // next line
    }
}

void RrdGlcd::displayChar(int row, int col, char c) {
    int x= col*6;
    // if this wraps the line ignore it
    if(x+6 > WIDTH) return;

    // convert row/column into y and x pixel positions based on font size
    renderChar(this->fb, c, x, row*8);
}

void RrdGlcd::renderGlyph(int xp, int yp, const uint8_t *g, int pixelWidth, int pixelHeight) {
    if(fb == NULL) return;
    // NOTE the source is expected to be byte aligned and the exact number of pixels
    // TODO need to optimize by copying bytes instead of pixels...
    int xf= xp%8;
    int rf= pixelWidth%8;
    int a= yp*16 + xp/8; // start address in frame buffer
    const uint8_t *src= g;
    if(xf == 0) {
        // If xp is on a byte boundary simply memcpy each line from source to dest
        uint8_t *dest= &fb[a];
        int n= pixelWidth/8; // bytes per line to copy
        if(rf != 0) n++; // if not a multiple of 8 pixels copy last byte as a byte
        if(n > 0) {
            for(int y=0;y<pixelHeight;y++) {
                memcpy(dest, src, n);
                src += n;
                dest+=16; // next line
            }
        }

        // TODO now handle ragged end if we have one but as we always render left to right we probably don't need to
        // if(rf != 0) {

        // }
        return;
    }

    // if xp is not on a byte boundary we do the slow pixel by pixel copy
    for(int y=0;y<pixelHeight;y++) {
        int m= 0x80;
        int b= *g++;
        for(int x=0;x<pixelWidth;x++) {
            a= (y+yp)*16 + (x+xp)/8;
            int p= 1<<(7-(x+xp)%8);
            if((b & m) != 0){
                fb[a] |= p;
            }else{
                fb[a] &= ~p;
            }
            m= m>>1;
            if(m == 0){
                m= 0x80;
                b= *g++;
            }
        }
    }
}

// copy frame buffer to graphic buffer on display
void RrdGlcd::fillGDRAM(const uint8_t *bitmap) {
    unsigned char i, y;
    for ( i = 0 ; i < 2 ; i++ ) {
        ST7920_CS();
        for ( y = 0 ; y < PAGE_HEIGHT ; y++ ) {
            ST7920_SET_CMD();
            ST7920_WRITE_BYTE(0x80 | y);
            if ( i == 0 ) {
                ST7920_WRITE_BYTE(0x80);
            } else {
                ST7920_WRITE_BYTE(0x80 | 0x08);
            }
            ST7920_SET_DAT();
            ST7920_WRITE_BYTES(bitmap, WIDTH/8); // bitmap gets incremented in this macro
        }
        ST7920_NCS();
    }
}

void RrdGlcd::refresh() {
    if(!inited || !dirty) return;
    fillGDRAM(this->fb);
    dirty= false;
}
