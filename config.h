#ifndef CONFIG_H
#define CONFIG_H

#include "editor.h"

enum Config_Keywords {

	// -- ui style --

    Config_font_height,
    Config_radius,
    Config_tab_width,

	// -- color --

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


	// -- keymaps --

	Config_command_mode,
	Config_insert_mode,
	Config_buffer_search_mode,
	Config_normal_mode,
	Config_visual_mode,

	Config_cursor_left,
	Config_cursor_right,
	Config_cursor_up,
	Config_cursor_down,

	Config_scroll_down,
	Config_scroll_up,

	Config_jump_to_end_of_file,
	Config_jump_to_start_of_line,
	Config_jump_to_end_of_line,
	Config_jump_to_first_non_white,

	Config_insert_at_end_of_line,
	Config_insert_at_first_non_white,

	Config_increase_font_size,
	Config_decrease_font_size,

	Config_copy_text,
	Config_paste_text,

	Config_autocomplete,
	Config_ui_confirm,

    Cfg_Keyword_Count,

};

union Config_Value {
	vec4 float4;
	f32  float1;
	u32  uint32;
	Ed_Keymap key;
};

global Config_Value
CONFIG_STATE [Cfg_Keyword_Count] = {};

#define cfg_u32(x)    CONFIG_STATE[Config_##x].uint32
#define cfg_f32(x)    CONFIG_STATE[Config_##x].float1
#define cfg_color(x)  CONFIG_STATE[Config_##x].float4
#define cfg_keymap(x) CONFIG_STATE[Config_##x].key

inline rune
keymap_to_rune(Ed_Keymap key) {
	if (key.flags & Keymap_Ctrl)
		return Ctrl(key.codepoint);
	return key.codepoint;
}

#define key_map(x) keymap_to_rune(cfg_keymap(x))

#include "embed.data"

#endif
