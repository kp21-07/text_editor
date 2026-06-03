#ifndef CONFIG_H
#define CONFIG_H

#include "editor.h"

const f32 FONT_HEIGHT = 22.0f;
const u64 TAB_WIDTH   = 2;

struct Color {
	global const u32 bg          = Hex(0x1B1816FF); // main background
	global const u32 bg_alt      = Hex(0x26211EFF); // panels, gutters
	global const u32 fg          = Hex(0xE3D7C3FF); // primary text
	global const u32 dim         = Hex(0x8C8276FF); // comments, inactive text
	global const u32 accent      = Hex(0xD19A66FF); // warm amber
	global const u32 cursor      = Hex(0xF0C674FF); // bright gold
	global const u32 error       = Hex(0xD16969FF); // muted red
};

#include "embed.dat"

#endif
