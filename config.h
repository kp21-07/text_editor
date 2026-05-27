#ifndef CONFIG_H
#define CONFIG_H

#include "editor.h"

///////////////////////////////
// ~geb: theme

const f32 FONT_HEIGHT = 22.0f;
const u64 TAB_WIDTH   = 4;

struct Color {
	global const u32 bg          = Hex(0x161311FF);
	global const u32 bg_alt      = Hex(0x211D1AFF);
	global const u32 fg          = Hex(0xD9C7A3FF);
	global const u32 dim         = Hex(0x7A6F62FF);
	global const u32 accent      = Hex(0xC8923BFF);
	global const u32 cursor      = Hex(0xF3C978FF);
	global const u32 error       = Hex(0xC8545DFF);
};

#include "embed.dat"

#endif
