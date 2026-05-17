#ifndef CONFIG_H
#define CONFIG_H

#include "editor.h"

///////////////////////////////
// ~geb: theme

struct Color {
	global const u32 bg          = Hex(0x0F1919FF);
	global const u32 bg_alt      = Hex(0x102121FF);
	global const u32 fg          = Hex(0xBFB7ABFF);
	global const u32 dim         = Hex(0x3E4451FF);

	global const u32 accent      = Hex(0x7A9E9FFF);
	global const u32 cursor      = Hex(0x4FA38CFF);
};

const f32 FONT_HEIGHT = 24.0f;

#endif
