#ifndef CONFIG_H
#define CONFIG_H

#include "editor.h"

///////////////////////////////
// ~geb: theme

const f32 FONT_HEIGHT = 24.0f;
const u64 TAB_WIDTH   = 4;

struct Color {
    // background
    global const u32 bg          = Hex(0x141312ff);
    global const u32 bg_alt      = Hex(0x1B1A18ff);

    // primary text
    global const u32 fg          = Hex(0xD4C4A8ff);

    // comments / inactive UI
    global const u32 dim         = Hex(0x6E665Bff);

    // highlights / selections / active UI
    global const u32 accent      = Hex(0xD9A441ff);

    // cursor
    global const u32 cursor      = Hex(0xF0C674ff);

    // diagnostics
    global const u32 error       = Hex(0xD14B5Aff);
};

#include "embed.dat"

#endif
