#ifndef CONFIG_H
#define CONFIG_H

#include "editor.h"

const f32 FONT_HEIGHT = 24.0f;
const u64 TAB_WIDTH   = 2;


global struct {
	vec4 background         = color(0x1A1B1EFF);
	vec4 foreground         = color(0xD8D9DAFF);
	vec4 background_dim     = color(0x121316FF);

	vec4 cursor             = color(0xE6B35AFF);
	vec4 cursor_text        = color(0x1A1B1EFF);

	vec4 line_highlight     = color(0x25272CFF);
	vec4 current_line       = color(0x2E3138FF);

	vec4 gutter             = color(0x16171AFF);
	vec4 gutter_foreground  = color(0x6B7078FF);

	vec4 border             = color(0x353941FF);

	vec4 status_line        = color(0x25272CFF);
	vec4 status_line_dim    = color(0x1C1D21FF);

	vec4 error              = color(0xE06C75FF);
	vec4 accent             = color(0x61AFEFFF);


	f32  radius = 8.0f;
} THEME;


#include "embed.dat"

#endif
