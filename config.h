#ifndef CONFIG_H
#define CONFIG_H

#include "editor.h"

const f32 FONT_HEIGHT = 26.0f;
const u64 TAB_WIDTH   = 2;

struct Theme {
	u32 background         = Hex(0x1A1B1EFF);
	u32 foreground         = Hex(0xD8D9DAFF);
	u32 background_dim     = Hex(0x121316FF);

	u32 cursor             = Hex(0xE6B35AFF);
	u32 cursor_text        = Hex(0x1A1B1EFF);

	u32 line_highlight     = Hex(0x25272CFF);
	u32 current_line       = Hex(0x2E3138FF);

	u32 gutter             = Hex(0x16171AFF);
	u32 gutter_foreground  = Hex(0x6B7078FF);

	u32 border             = Hex(0x353941FF);

	u32 status_line        = Hex(0x25272CFF);
	u32 status_line_dim    = Hex(0x1C1D21FF);

	u32 error              = Hex(0xE06C75FF);
	u32 accent             = Hex(0x61AFEFFF);
};


struct Config {
	Theme theme;
};

global Config g_config;

#include "embed.dat"

#endif
