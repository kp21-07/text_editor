#include "editor.h"
#include "config.h"

#include <math.h>

enum UI_Classes {
	Status_Line,
	Command_Box,
	Pane,
	Class_Count,
};


funcdef string
format_user_input(rune codepoint, Arena *frame_arena, Ed_Cmd *post)
{
	if (codepoint == '\n') {
		Buffer *buf = ed_active();
		u64 line_index = buffer_line_index_at(buf, buf->cursor);
		Range_u64 range = buffer_line_range(buf, line_index);
		range.end = Min(range.end, buf->cursor);
		string line = buffer_slice(buf, frame_arena, range);

		u64 i=0;
		for (;i<line.len && is_space(line[i]); ++i)
			;

		rune before = buffer_char_at(ed_active(), buffer_cursor(ed_active()) - 1);
		rune after = buffer_char_at(ed_active(), buffer_cursor(ed_active()));

		string result = string_concat(frame_arena, S("\n"), line.range(0, i));

		if (char_kind(before) == Char_Open) {
			bool between_pair = (char_get_pair(before) == after);

			if (!between_pair) {
				result = string_concat(frame_arena, result, S("\t"));
			}
		}

		return result;
	}
	else if(codepoint == '\t' || unicode_visual_rune(codepoint)) {
		CharKind kind = char_kind(codepoint);
		rune at_cursor = buffer_char_at(ed_active(), buffer_cursor(ed_active()));

		if (kind == Char_Open) {
			if (is_space(at_cursor) || 
				char_kind(at_cursor) != Char_Open) {
				rune pair = char_get_pair(codepoint);
				*post = move_cursor(Direction::Left, 1);

				string left = utf8_encode(codepoint, frame_arena);
				string right = utf8_encode(pair, frame_arena);
				return string_concat(frame_arena, left, right);
			}
		}
		else if (kind == Char_Close || kind == Char_Quote) {
			if (at_cursor == codepoint) {
				*post = move_cursor(Direction::Right, 1);
				return {};
			}
			if (kind == Char_Quote) {
				rune pair = char_get_pair(codepoint);
				*post = move_cursor(Direction::Left, 1);
				string left = utf8_encode(codepoint, frame_arena);
				string right = utf8_encode(pair, frame_arena);
				return string_concat(frame_arena, left, right);
			}
		}

		return utf8_encode(codepoint, frame_arena);
	}
	return {};
}

global UI_Config STYLES[Class_Count];

funcdef void
set_default_style() {
	STYLES[Status_Line] = {
		UI_Clip_Children,
		{
			{ Size_Fill, 1.0f },
			{ Size_Fixed, gfx_line_height() }
		},
		Pad(2),

		4.0f, // rounding
		0.0f, // border

		Color::dim, // color
		0x0, // text color
		0x0, // border color

		{}, // text

		2, // gap
		Layout_Row,
		Align_Start,
	};

	STYLES[Command_Box] = {
		UI_Clip_Children,
		{
			{ Size_Fill, 3.0f },
			{ Size_Fit }
		},
		Pad(10),

		10.0f, // rounding
		1.0f, // border

		Color::bg, // color
		0x0, // text color
		Color::dim, // border color

		{}, // text

		5, // gap
		Layout_Col,
		Align_Center,
	};

	STYLES[Pane] = {
		UI_Invisible,
		{
			{ Size_Fill, 1.0f },
			{ Size_Fill, 1.0f }
		},
		Pad(5),

		0.0f, // rounding
		0.0f, // border

		0x0, // color
		0x0, // text color
		0x0, // border color

		{}, // text

		1, // gap
		Layout_Row,
		Align_Center,
	};
}

funcdef Ed_Cmd
parse_command(slice<string> args)
{
	if (!args.len)
		return {};
	
	string main_arg = args[0];

	bool int_ok = false;
	s64 i_val = string_to_int(main_arg, &int_ok);

	if (int_ok && i_val >= 0) {
		return jump_to_line((u64) i_val);
	}

	if (string_equal(main_arg, cmd_function(Cmd_Buffer_Open))) {
		auto params = args.range(1, args.len);
		if (params.len == 1) {
			OS_FileData data = os_file_data(params[0]);
			if (Flag_Check(data.flags, File_Directory))
			{
				return open_workspace(params[0]);
			}
		}
		return open_buffer(params);
	}
	else if (string_equal(main_arg, cmd_function(Cmd_Buffer_Close))) {
		slice<string> paths = args.range(1, args.len);
		if (paths.len == 0 && ed_active() != nullptr) {
			paths = {
				&ed_active()->path,
				1
			};
			return close_buffer(paths);
		}

		return close_buffer(paths);
	} else if (string_equal(main_arg, cmd_function(Cmd_Buffer_Save))) {
		Ed_Cmd cmd = save_buffer();
		return cmd;
	}

	return {};
}

void entry_point(slice<string> args)
{
	os_init();
	defer(os_deinit());
    
	OS_Handle win = os_open_window(S("editor"))	;
	defer(os_close_window(win));

	ed_init();
	defer(ed_deinit());
    
	gfx_init(win, persist_arena());
	defer(gfx_deinit());
    
	ui_init(frame_arena());
	set_default_style();

	while(!os_window_should_close(win)) {
		arena_free(frame_arena());
        
		OS_Input input = os_prepare_frame(win);
		Ed_Mode curr_mode = ed_mode();

		if (curr_mode == Ed_Mode::Normal) {
			Ed_Cmd cmd = {};

			switch (input.codepoint) {
				case ':': cmd = change_mode(Ed_Mode::Command); break;
				case 'i': cmd = change_mode(Ed_Mode::Insert); break;
				case 'h': cmd = move_cursor(Direction::Left, 1); break;
				case 'j': cmd = move_cursor(Direction::Down, 1); break;
				case 'k': cmd = move_cursor(Direction::Up, 1); break;
				case 'l': cmd = move_cursor(Direction::Right, 1); break;

				case 'J': cmd = move_cursor(Direction::Down, 10); break;
				case 'K': cmd = move_cursor(Direction::Up, 10); break;

				case '0': {
					Buffer *active = ed_active();
					u64 line_index = buffer_line_index_at(active, active->cursor);
					auto range = buffer_line_range(active, line_index);
					cmd = move_cursor(Direction::Absolute, range.begin);
				} break;

				case '$': case 'A': {
					Buffer *active = ed_active();
					u64 line_index = buffer_line_index_at(active, active->cursor);
					auto range = buffer_line_range(active, line_index);
					cmd = move_cursor(Direction::Absolute, range.end);

					if (input.codepoint == 'A') {
						ed_exec_command(cmd);
						cmd = change_mode(Ed_Mode::Insert);
					}
				} break;

				case '_': case 'I':
				{
					Buffer *active = ed_active();
					u64 line_index = buffer_line_index_at(active, active->cursor);
					auto range = buffer_line_range(active, line_index);
					string line = buffer_slice(active, frame_arena(), range);

					u64 i=0;
					for (;i<line.len && is_space(line[i]); ++i)
						;

					cmd = move_cursor(Direction::Absolute, range.begin + i);
					if (input.codepoint == 'I') {
						ed_exec_command(cmd);
						cmd = change_mode(Ed_Mode::Insert);
					}
				} break;

			}

			ed_exec_command(cmd);
		}
		else if (curr_mode == Ed_Mode::Command) {
			switch (input.codepoint) {
				case '\x1b': {
					Ed_Cmd cmd = change_mode(Ed_Mode::Normal);
					ed_exec_command(cmd);
				} break;

				case '\x7F': { // delete
					Ed_Cmd cmd = delete_string(Direction::Right, 1);
					ed_exec_command(cmd);
				} break;

				case '\b': { // backspace
					Ed_Cmd cmd = delete_string(Direction::Left, 1);
					ed_exec_command(cmd);
				} break;

				case '\n': {
					Temp t0 = temp_begin(scratch());
					defer(temp_end(t0));

					slice<string> args = ed_command_strings(scratch());

					Ed_Cmd cmd = parse_command(args);
					ed_exec_command(cmd);

					cmd = change_mode(Ed_Mode::Normal);
					ed_exec_command(cmd);
				}

				default: {
					if (!unicode_visual_rune(input.codepoint))
						break;

					Ed_Cmd cmd = insert_string(
						utf8_encode(input.codepoint, frame_arena())
					);
					ed_exec_command(cmd);
				} break;
			}
		}else if (curr_mode == Ed_Mode::Insert) {
			switch (input.codepoint) {
				case '\x1b': { // escape
					Ed_Cmd cmd = change_mode(Ed_Mode::Normal);
					ed_exec_command(cmd);
				} break;

				case '\x7F': { // delete
					Ed_Cmd cmd = delete_string(Direction::Right, 1);
					ed_exec_command(cmd);
				} break;

				case '\b': { // backspace
					Ed_Cmd cmd = delete_string(Direction::Left, 1);
					ed_exec_command(cmd);
				} break;

				default: {
					Ed_Cmd post = {};
					string fmt = format_user_input(input.codepoint, frame_arena(), &post);
					if (fmt.len) {
						Ed_Cmd cmd = insert_string(fmt);
						ed_exec_command(cmd);
					}
					ed_exec_command(post);
				} break;
			}
		}
        
		gfx_begin();
		defer(gfx_submit());
        
		ivec2 win_size = os_window_size(win);
		Rect win_rect = { {0, 0}, { (f32) win_size.x, (f32) win_size.y } };
		gfx_set_viewport(win_size.x, win_size.y);
        
		gfx_push_clip(win_rect, frame_arena());
		defer(gfx_pop_clip());
        
		UI_Config root = {};
		root.layout = Layout_Col;
		root.color = Color::bg;
		root.gap = 1.0f;

		ui_begin_frame(win_rect, root);
	
		UI_Box *panel = nullptr;

		UI(STYLES[Pane]) {
			panel = __this_box__;
		}

		UI(STYLES[Status_Line]) {
			ui_text(modal_string(ed_mode()), Color::bg, Align_Start, Size_Fill);


	
			string file_name = ed_active() ? ed_active()->path : S("- no file -");
			string directory = ed_directory();

			string right = string_format(frame_arena(), "%.*s | %.*s", S_FMT(directory), S_FMT(file_name));
			ui_text(right, Color::bg, Align_End, Size_Fill);
		}
        
		ui_end_frame();
		ui_draw();

		draw_buffer_view(ed_active(), panel->rect);

		if (ed_mode() == Ed_Mode::Command) {
			draw_buffer_view(ed_active(), panel->rect);
			root.padding = Pad_XY(0, 50);
			root.layout = Layout_Row;
			root.color = Hex(0x000000aa);

			ui_begin_frame(win_rect, root);

			UI_Config pad = {
				UI_Invisible,
				{
					{Size_Fill, 1.0f},
					{Size_Fill, 1.0f}
				}
			};
			pad.layout = Layout_Row;
			UI(pad);
			UI(STYLES[Command_Box]) {
				pad.size.h = {Size_Fit};
				UI(pad) {
					string cmd_string = ed_command_string();
					ui_text(cmd_string.len ? cmd_string : S("Run Command.."), Color::dim);

					if (cmd_string.len)  {
						UI_Config cursor = {
							0,
							{
								{Size_Fixed, 2},
								{Size_Fill, 1.0}
							}
						};
						cursor.color = Color::fg;
						UI(cursor);
					}
				}

			}
			UI(pad);

			ui_end_frame();
			ui_draw();
		}
	}
}
