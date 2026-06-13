#ifndef CONFIG_H
#define CONFIG_H

#include "editor.h"

enum Config_Keywords {
    Config_font_height,
    Config_radius,
    Config_tab_width,

    Config_background,
    Config_foreground,
    Config_background_dim,

    Config_cursor,
    Config_cursor_text,

    Config_selection,

    Config_line_highlight,
    Config_current_line,

    Config_gutter,
    Config_gutter_foreground,

    Config_border,

    Config_status_line,
    Config_status_line_dim,

    Config_error,
    Config_warning,
    Config_success,

    Config_accent,

    Config_comment,
    Config_keyword,
    Config_string,
    Config_number,
    Config_type,
    Config_function,
    Config_operator,
    Config_constant,
    Config_preprocessor,
    Config_macro,

    Config_search_match,
    Config_bracket_match,
    Config_indent_guide,

    Cfg_Keyword_Count,
};

union Config_Value {
	vec4 float4;
	f32  float1;
	u32  uint32;
	rune codepoint;
};

global Config_Value
CONFIG_STATE [Cfg_Keyword_Count] = {};

#define cfg_u32(x)    CONFIG_STATE[Config_##x].uint32
#define cfg_f32(x)    CONFIG_STATE[Config_##x].float1
#define cfg_color(x)  CONFIG_STATE[Config_##x].float4
#define cfg_rune(x)   CONFIG_STATE[Config_##x].codepoint

global const string
CONFIG_KEYWORD_STR[Cfg_Keyword_Count] = {
};

#include "embed.data"

#endif
