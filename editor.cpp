#include "editor.h"
#include "config.h"

global struct {
	Arena *persist_arena;
	Arena *frame_arena;
	Arena *modal_arena;

	Ed_Mode mode;

	Arena  *workspace_arena;
	Buffer *active_buffer;
	string  working_dir;

	list<u8> cmd_string;
	Buffer_Map buffer_map;

	slice<string> open_buffers;
	u64           ui_select_index;

	Range_u64     yank_region;

} ed_ctx;

funcdef Ed_Keymap
ed__string_to_keymap(string s, bool *ok)
{
	Ed_Keymap map = {};
	s = string_strip(s);

	if (ok)
		*ok = false;

	if (s.len == 0)
		return map;

	if (string_starts_with(s, S("Ctrl+")))
	{
		map.flags |= Keymap_Ctrl;
		s = s.range(5, s.len);
	}

	if (string_equal(s, S("Tab")))
		map.codepoint = '\t';
	else if (string_equal(s, S("Esc")))
		map.codepoint = 27;
	else if (string_equal(s, S("Space")))
		map.codepoint = ' ';
	else if (string_equal(s, S("Backspace")))
		map.codepoint = '\b';
	else if (string_equal(s, S("Return")))
		map.codepoint = '\n';
	else {
		int width = 0;
		rune r = utf8_decode(s, &width);

		if ((u64) width != s.len) {
			return map;
		}
		map.codepoint = r;
	}

	if (ok)
		*ok = true;

	return map;
}

funcdef void
ed__load_config_file()
{
	Temp t1 = temp_begin(scratch(0, 0));
	defer(temp_end(t1));

	string exec_path = os_get_exec_directory(t1.arena);
	string config_path = string_concat(t1.arena, exec_path, S("/config.data"));

	string data = string_from_bytes(
		os_load_entire_file(t1.arena, config_path)
	);

	slice<string> lines = string_to_lines(t1.arena, data);

	for (u64 i=0; i<lines.len; ++i) {
		Temp t2 = temp_begin(t1.arena);
		defer(temp_end(t2));

		string line = string_strip(lines[i]);
		if (!line.len || line[0] == '#')
			continue;
	
		slice<string> tokens = string_split(line, t2.arena);

		if (tokens.len != 2)
			continue;

		string keyword = tokens[0];
		string value   = tokens[1];

#define parse_cfg_value(key_str, type_suffix, parse_func, return_type, var_name, condition) \
    else if (string_equal(keyword, S(key_str))) {                                           \
        bool ok = false;                                                                    \
        return_type v = (return_type) parse_func(value, &ok);                               \
        if (ok && (condition)) {                                                            \
            cfg_##type_suffix(var_name) = v;                                                \
        }                                                                                   \
    }
		
#define parse_cfg_f32(name, cond)   parse_cfg_value(#name, f32,  string_to_f32,   f32,  name, cond)
#define parse_cfg_s32(name, cond)   parse_cfg_value(#name, u32,  string_to_s32,   s32,  name, cond)
#define parse_cfg_color(name)       parse_cfg_value(#name, color, string_to_color, vec4, name, true)
#define parse_cfg_keymap(name)      parse_cfg_value(#name, keymap, ed__string_to_keymap, Ed_Keymap, name, true)

		if (false) {}
		parse_cfg_f32(font_height, v > 5.0f)
		parse_cfg_f32(radius, v >= 0.0f)
		parse_cfg_s32(tab_width, v >= 1)

		parse_cfg_color(background)
		parse_cfg_color(foreground)
		parse_cfg_color(background_dim)

		parse_cfg_color(cursor)
		parse_cfg_color(cursor_text)

		parse_cfg_color(selection)

		parse_cfg_color(line_highlight)
		parse_cfg_color(current_line)

		parse_cfg_color(gutter)
		parse_cfg_color(gutter_foreground)

		parse_cfg_color(border)

		parse_cfg_color(status_line)
		parse_cfg_color(status_line_dim)

		parse_cfg_color(error)
		parse_cfg_color(warning)
		parse_cfg_color(success)

		parse_cfg_color(accent)

		parse_cfg_color(comment)
		parse_cfg_color(keyword)
		parse_cfg_color(string)
		parse_cfg_color(number)
		parse_cfg_color(type)
		parse_cfg_color(function)
		parse_cfg_color(operator)
		parse_cfg_color(constant)
		parse_cfg_color(preprocessor)
		parse_cfg_color(macro)

		parse_cfg_color(search_match)
		parse_cfg_color(bracket_match)
		parse_cfg_color(indent_guide)

		parse_cfg_keymap(command_mode)
		parse_cfg_keymap(insert_mode)
		parse_cfg_keymap(buffer_search_mode)
		parse_cfg_keymap(normal_mode)
		parse_cfg_keymap(visual_mode)

		parse_cfg_keymap(cursor_left)
		parse_cfg_keymap(cursor_down)
		parse_cfg_keymap(cursor_up)
		parse_cfg_keymap(cursor_right)

		parse_cfg_keymap(scroll_down)
		parse_cfg_keymap(scroll_up)

		parse_cfg_keymap(jump_to_end_of_file)
		parse_cfg_keymap(jump_to_start_of_line)
		parse_cfg_keymap(jump_to_end_of_line)
		parse_cfg_keymap(jump_to_first_non_white)

		parse_cfg_keymap(insert_at_end_of_line)
		parse_cfg_keymap(insert_at_first_non_white)

		parse_cfg_keymap(increase_font_size)
		parse_cfg_keymap(decrease_font_size)

		parse_cfg_keymap(copy_text)
		parse_cfg_keymap(paste_text)

		parse_cfg_keymap(autocomplete)
		parse_cfg_keymap(ui_confirm)

		else {}

#undef parse_cfg_value
#undef parse_cfg_f32
#undef parse_cfg_s32
#undef parse_cfg_color
	}
}


funcdef void
ed__init_workspace()
{
	buffer_map_clear(&ed_ctx.buffer_map);

	arena_free(ed_ctx.workspace_arena);
	ed_ctx.working_dir = os_get_working_dir(ed_ctx.workspace_arena);
	ed_ctx.buffer_map = buffer_map_make(ed_ctx.workspace_arena, 128);
	ed_ctx.active_buffer = nullptr;
}

funcdef void
ed_init()
{
	ed_ctx.persist_arena   = arena_make(MB(4));
	ed_ctx.frame_arena     = arena_make(MB(1));
	ed_ctx.modal_arena     = arena_make(KB(256));
	ed_ctx.workspace_arena = arena_make(KB(512));

	/////////////
	// fallback config

	cfg_f32(font_height) = 24.0f;
	cfg_f32(radius)      = 8.0f;
	cfg_u32(tab_width)   = 2;

	cfg_color(background) = color(0x1E2326FF);
	cfg_color(foreground) = color(0xD3C6AAFF);
	cfg_color(background_dim) = color(0x272E33FF);
	cfg_color(cursor) = color(0xA7C080AA);
	cfg_color(cursor_text) = color(0x1A1A1AFF);
	cfg_color(selection) = color(0x7FBBB344);
	cfg_color(line_highlight) = color(0x2C4841FF);
	cfg_color(current_line) = color(0x2D3539FF);
	cfg_color(gutter) = color(0x7A8478FF);
	cfg_color(gutter_foreground) = color(0xAAAAAAFF);
	cfg_color(border) = color(0x9DA9A0AA);
	cfg_color(status_line) = color(0x1A1A1AFF);
	cfg_color(status_line_dim) = color(0x0A0A0AFF);
	cfg_color(error) = color(0x772222FF);
	cfg_color(warning) = color(0x986032FF);
	cfg_color(success) = color(0x227722FF);
	cfg_color(accent) = color(0x7A8478FF);
	cfg_color(comment) = color(0x3DDF23FF);
	cfg_color(keyword) = color(0xFFFFFFFF);
	cfg_color(string) = color(0x0FDFAFFF);
	cfg_color(number) = color(0xD699B5FF);
	cfg_color(type) = color(0x98FB98FF);
	cfg_color(function) = color(0xD3B58DFF);
	cfg_color(operator) = color(0xE0AD82FF);
	cfg_color(constant) = color(0x7FFFD4FF);
	cfg_color(preprocessor) = color(0xE67D74FF);
	cfg_color(macro) = color(0xE0AD82FF);
	cfg_color(search_match) = color(0xCD6889FF);
	cfg_color(bracket_match) = color(0xFCEDFC26);
	cfg_color(indent_guide) = color(0xFCEDFC26);

	cfg_keymap(command_mode)       = Ed_Keymap { 0, ':' };
	cfg_keymap(insert_mode)        = Ed_Keymap { 0, 'i' };
	cfg_keymap(buffer_search_mode) = Ed_Keymap { 0, '\t' };
	cfg_keymap(normal_mode)        = Ed_Keymap { 0, '\x1b' };
	cfg_keymap(visual_mode)        = Ed_Keymap { 0, 'v' };

	cfg_keymap(cursor_up)    = Ed_Keymap { 0, 'k' };
	cfg_keymap(cursor_down)  = Ed_Keymap { 0, 'j' };
	cfg_keymap(cursor_right) = Ed_Keymap { 0, 'l' };
	cfg_keymap(cursor_left)  = Ed_Keymap { 0, 'h' };

	cfg_keymap(scroll_up)   = Ed_Keymap { 0, 'K' };
	cfg_keymap(scroll_down) = Ed_Keymap { 0, 'J' };

	cfg_keymap(jump_to_end_of_file)   = Ed_Keymap { 0, 'G' };
	cfg_keymap(jump_to_start_of_line) = Ed_Keymap { 0, '0' };
	cfg_keymap(jump_to_end_of_line)     = Ed_Keymap { 0, '$' };
	cfg_keymap(jump_to_first_non_white) = Ed_Keymap { 0, '_' };

	cfg_keymap(insert_at_end_of_line)     = Ed_Keymap { 0, 'A' };
	cfg_keymap(insert_at_first_non_white) = Ed_Keymap { 0, 'I' };

	cfg_keymap(copy_text)  = Ed_Keymap { 0, 'y' };
	cfg_keymap(paste_text) = Ed_Keymap { 0, 'p' };

	cfg_keymap(increase_font_size) = Ed_Keymap { 0, '+' };
	cfg_keymap(decrease_font_size) = Ed_Keymap { 0, '-' };

	cfg_keymap(autocomplete) = Ed_Keymap { 0, '\t' };
	cfg_keymap(ui_confirm) = Ed_Keymap { 0, '\n' };

	/////////////

	ed__init_workspace();
	ed__load_config_file();
}

funcdef void
ed_deinit()
{
	arena_delete(ed_ctx.workspace_arena);
	arena_delete(ed_ctx.modal_arena);
	arena_delete(ed_ctx.frame_arena);
	arena_delete(ed_ctx.persist_arena);
}

funcdef Ed_Mode
ed_mode() 
{
	return ed_ctx.mode;
}


funcdef Buffer *
ed_active()
{
	return ed_ctx.active_buffer;
}

funcdef string
ed_directory()
{
	return ed_ctx.working_dir;
}

funcdef string
modal_string(Ed_Mode mode)
{
	switch (mode)
	{
		case Ed_Mode::Normal:  return S("normal");
		case Ed_Mode::Insert:  return S("insert");
		case Ed_Mode::Command: return S("command");
		case Ed_Mode::Buffer_Search: return S("buffer search");
		case Ed_Mode::Visual: return S("visual");
	}

	return {};
}


funcdef string
ed_command_string()
{
	return ed_ctx.cmd_string.view();
}

funcdef slice<string>
ed_command_strings(Arena *arena)
{
	return string_split(ed_ctx.cmd_string.view(), arena);
}

funcdef slice<string>
ed_open_buffers()
{
	if (ed_ctx.open_buffers.raw == nullptr) {
		// called when outside buffer search mode, can frame allocate for now.
		return buffer_map_get_paths(&ed_ctx.buffer_map, ed_ctx.frame_arena);
	}

	return ed_ctx.open_buffers;
}

funcdef Range_u64
ed_get_selection_region()
{
    u64 a = ed_ctx.yank_region.begin;
    u64 b = ed_ctx.yank_region.end;

    if (a <= b)
        return { a, b + 1 };

    return { b, a + 1 };
}

funcdef void
ed_exec_command(Ed_Cmd cmd)
{
    Ed_CmdKind kind = cmd.kind;
    switch (kind) {
		case Cmd_Buffer_Open: {
			slice<string> args = cmd.arg_strings;
			for (u64 i = 0; i < args.len; ++i) {
				string path = string_copy(ed_ctx.workspace_arena, args[i]);
				OS_FileData file_data = os_file_data(path);

				if (file_data.flags & File_Directory)
					continue;

				Buffer *buf = buffer_map_get(&ed_ctx.buffer_map, path);

				if (buf == nullptr) {
					Buffer new_buf = {};
					buffer_init(&new_buf, path);
					buf = buffer_map_insert(&ed_ctx.buffer_map, new_buf);
				}

				if (i == args.len - 1)
					ed_ctx.active_buffer = buf;
			}
		} break;

		case Cmd_Buffer_Close: {
			slice<string> paths = cmd.arg_strings;
			bool closed_active = false;

			if (ed_active()) {
				for (u64 i = 0; i < paths.len; ++i) {
					if (string_equal(paths[i], ed_ctx.active_buffer->path)) {
						closed_active = true;
						break;
					}
				}
			}

			for (u64 i = 0; i < paths.len; ++i)
			{
				string path = paths[i];
				if (string_equal(path, S("*")))
				{

					return;
				}
				buffer_map_remove(&ed_ctx.buffer_map, paths[i]);
			}

			if (!closed_active)
				break;

			ed_ctx.active_buffer = nullptr;

			slice<Buffer> table = ed_ctx.buffer_map.table;
			for (u64 i = 0; i < table.len; ++i) {
				if (Flag_Check(table[i].flags, Buffer_Occupied)) {
					ed_ctx.active_buffer = &table[i];
					break;
				}
			}
		} break;

        case Cmd_Buffer_Save: {
			Buffer *active = ed_active();
			if (!active)
				break;

			string data = active->data.view();
			string path = cmd.arg_string.len ? cmd.arg_string : active->path;

			if (path.len) 
			{
				bytes data_b = {
					(u8 *) data.raw,
					data.len
				};
				bool ok = os_write_to_file(path, data_b);
			}
        } break;

		case Cmd_Mode_Change: {
			Range_u64 sel = ed_get_selection_region();

			ed_ctx.open_buffers = {};
			ed_ctx.cmd_string = {};
			ed_ctx.ui_select_index = 0;
			ed_ctx.yank_region = {};

			switch(cmd.arg_mode) {
			case Ed_Mode::Insert:
				if (ed_active() == nullptr)
					return;

				if (ed_ctx.mode == Ed_Mode::Visual) {
					ed_exec_command(move_cursor(Direction::Absolute, sel.begin));
					ed_exec_command(delete_string(Direction::Right, sel.end - sel.begin));
				}
			break;

			case Ed_Mode::Command:
				ed_ctx.cmd_string = list_make(alloc_slice(ed_ctx.modal_arena, u8, 128));
			break;

			case Ed_Mode::Buffer_Search:
				ed_ctx.open_buffers = buffer_map_get_paths(
					&ed_ctx.buffer_map,
					ed_ctx.modal_arena
				);
				ed_ctx.cmd_string = list_make(
					alloc_slice(ed_ctx.modal_arena, u8, 128)
				);
			break;

			case Ed_Mode::Visual:
			{
				u64 cursor = buffer_cursor(ed_active());
				ed_ctx.yank_region = { cursor, cursor };
			} break;

			default:
			break;
			}

			ed_ctx.mode = cmd.arg_mode;
			arena_free(ed_ctx.modal_arena);	
		} break;

		case Cmd_Jump_To_EoF:
		{
			Buffer *active = ed_active();
			u64 line_idx = buffer_line_count(active);
			ed_exec_command(jump_to_line(line_idx));
		} break;

		case Cmd_Jump_To_Star_Of_Line:
		{
			Buffer *active = ed_active();
			u64 line_index = buffer_line_index_at(active, buffer_cursor(active));
			auto range = buffer_line_range(active, line_index);
			ed_exec_command(move_cursor(Direction::Absolute, range.begin));
		} break;

		case Cmd_Jump_To_End_Of_Line:
		{
			Buffer *active = ed_active();
			u64 line_index = buffer_line_index_at(active, buffer_cursor(active));
			auto range = buffer_line_range(active, line_index);
			ed_exec_command(move_cursor(Direction::Absolute, range.end));
		} break;

		case Cmd_Jump_To_First_Non_White:
		{
			Buffer *active = ed_active();
			u64 line_index = buffer_line_index_at(active, buffer_cursor(active));
			auto range = buffer_line_range(active, line_index);
			string line = buffer_slice(active, frame_arena(), range);

			u64 i=0;
			for (;i<line.len && is_space(line[i]); ++i)
				;

			ed_exec_command(move_cursor(Direction::Absolute, range.begin + i));
		} break;

		case Cmd_Cursor_Move:
		{
			buffer_move_cursor(ed_ctx.active_buffer, cmd.arg_u64, cmd.arg_dir);

			if (ed_mode() == Ed_Mode::Visual) {
				ed_ctx.yank_region.end = buffer_cursor(ed_active());
			}
		} break;

		case Cmd_Insert_String: {
			Ed_Mode mode = ed_mode();
			if (mode == Ed_Mode::Command || mode == Ed_Mode::Buffer_Search) {
				bytes input_bytes = {(u8 *)cmd.arg_string.raw, cmd.arg_string.len};
				append_slice(&ed_ctx.cmd_string, input_bytes);
			} else {
				buffer_insert(ed_ctx.active_buffer, cmd.arg_string);
			}
		} break;

		case Cmd_Delete_String:
			if (ed_mode() == Ed_Mode::Command || ed_mode() == Ed_Mode::Buffer_Search) {
				if (ed_ctx.cmd_string.len >= cmd.arg_u64)	
					ed_ctx.cmd_string.len -= cmd.arg_u64;
			} else {
				buffer_delete(ed_ctx.active_buffer, cmd.arg_u64, cmd.arg_dir);
			}
		break;

		case Cmd_Jump_To_Line: {
			Buffer *buf = ed_active();
			u64 line_index = Min(cmd.arg_u64, buf->line_tbl.len);
			if (line_index > 0)
				line_index -= 1;

			auto range  = buffer_line_range(buf, line_index);
			ed_exec_command(move_cursor(Direction::Absolute, range.begin));
		} break;


        case Cmd_Workspace_Open: {
			string path = cmd.arg_string;
			os_set_working_dir(path);
			ed__init_workspace();
        } break;
        
		case Cmd_Reload: {
			if (ed_active() && ed_active()->file_kind == OS_FileKind::Config) {
				ed_exec_command(save_buffer());
			}
			ed__load_config_file();
        } break;

		case Cmd_Copy_Text:
		{
			Temp temp = temp_begin(frame_arena());
			defer(temp_rollback(temp));

			u64 buf_len = ed_active()->data.len;
			Range_u64 sel = ed_get_selection_region();
			sel.begin = Min(buf_len, sel.begin);
			sel.end   = Min(buf_len, sel.end);

			string str = buffer_slice(ed_active(), temp.arena, sel);
			str = string_to_cstring(temp.arena, str);
			os_set_clipboard_string(str);
		} break;

        case Cmd_Exit: {
        } break;

        default: break;
    }
}

funcdef Arena *
frame_arena()
{
	return ed_ctx.frame_arena;
}

funcdef Arena *
persist_arena()
{
	return ed_ctx.persist_arena;
}

//////////////////////////////////


funcdef Ed_Cmd
open_workspace(string path)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Workspace_Open;
	cmd.arg_string = path;
	return cmd;
}

funcdef Ed_Cmd
open_buffer(slice<string> paths)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Buffer_Open;
	cmd.arg_strings = paths;
	return cmd;
}

funcdef Ed_Cmd
close_buffer(slice<string> paths)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Buffer_Close;
	cmd.arg_strings = paths;
	return cmd;
}

funcdef Ed_Cmd
save_buffer(string to)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Buffer_Save;
	cmd.arg_string = to;
	return cmd;
}

funcdef Ed_Cmd
change_mode(Ed_Mode to)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Mode_Change;
	cmd.arg_mode = to;
	return cmd;
}

funcdef Ed_Cmd
move_cursor(Direction dir, u64 count)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Cursor_Move;
	cmd.arg_dir = dir;	
	cmd.arg_u64 = count;
	return cmd;
}


funcdef Ed_Cmd
insert_string(string str)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Insert_String;
	cmd.arg_string = str;
	return cmd;
}


funcdef Ed_Cmd
delete_string(Direction dir, u64 count)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Delete_String;
	cmd.arg_dir = dir;	
	cmd.arg_u64 = count;
	return cmd;
}

funcdef Ed_Cmd
jump_to_line(u64 line)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Line;
	cmd.arg_u64 = line;
	return cmd;
}

funcdef Ed_Cmd
reload_config()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Reload;
	return cmd;
}

funcdef Ed_Cmd
copy_selected()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Copy_Text;
	return cmd;
}


funcdef Ed_Cmd
jump_to_eof()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_EoF;
	return cmd;
}


funcdef Ed_Cmd
jump_to_start_of_line()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Star_Of_Line;
	return cmd;
}

funcdef Ed_Cmd
jump_to_end_of_line()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_End_Of_Line;
	return cmd;
}

funcdef Ed_Cmd 
jump_to_first_non_white()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_First_Non_White;
	return cmd;
}

funcdef string
cmd_function(Ed_CmdKind kind)
{
	switch (kind) {
		case Cmd_Buffer_Open:             return S("open");
		case Cmd_Buffer_Close:            return S("close");
		case Cmd_Buffer_Save:             return S("save");
		case Cmd_Exit:                    return S("exit");
		case Cmd_Reload:                  return S("reload");
		case Cmd_Jump_To_EoF:             return S("jump_to_eof");
		case Cmd_Jump_To_Star_Of_Line:    return S("jump_to_start_of_line");
		case Cmd_Jump_To_End_Of_Line:     return S("jump_to_end_of_line");
		case Cmd_Jump_To_First_Non_White: return S("jump_to_first_non_white");
		default:
			return S("");
	}
}
