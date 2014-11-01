#ifndef FONT5X8_H
#define FONT5X8_H

#include <stdint.h>

// 5x8 font each byte is consecutive x bits left aligned then each subsequent byte is Y 8 bytes per character
extern const uint8_t font5x8[256*8];

#endif