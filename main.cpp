#include "editor.h"
#include "config.h"

funcdef string format_user_input(rune codepoint, Ed_Cmd *post);
funcdef Ed_Cmd parse_command (slice<string> args);

void entry_point(slice<string> args)
{
	os_init();
	defer(os_deinit());

	ed_init();
	defer(ed_deinit());

	ed_exec_command(open_buffer(args.range(1, args.len)));
    
	OS_Handle win = os_open_window(S("editor"));
	defer(os_close_window(win));
    
	gfx_init(win, persist_arena(), frame_arena());
	defer(gfx_deinit());

	ui_init(persist_arena(), frame_arena());
    
	while(!os_window_should_close(win)) 
	{
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

				case 'G': {
					Buffer *active = ed_active();
					u64 line_idx = buffer_line_count(active);
					cmd = jump_to_line(line_idx);
				} break;

				case '0': {
					Buffer *active = ed_active();
					u64 line_index = buffer_line_index_at(active, buffer_cursor(active));
					auto range = buffer_line_range(active, line_index);
					cmd = move_cursor(Direction::Absolute, range.begin);
				} break;

				case '$': case 'A': {
					Buffer *active = ed_active();
					u64 line_index = buffer_line_index_at(active, buffer_cursor(active));
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
					u64 line_index = buffer_line_index_at(active, buffer_cursor(active));
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

				case 'L': {
					OS_TimeStamp t1 = os_time_now();
					defer(
						printf("ms%f\n", os_time_diff(t1, os_time_now()).seconds);
					);

					// slice<string> files = os_list_files(frame_arena(), ed_directory());
				} break;

				case '-': {
					f32 curr_height = gfx_get_font_height();
					gfx_set_font_height(Max(curr_height - 2, 12));
				} break;

				case '+' : {
					f32 curr_height = gfx_get_font_height();
					gfx_set_font_height(Min(curr_height + 2, 120));
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
					string fmt = format_user_input(input.codepoint, &post);
					if (fmt.len) {
						Ed_Cmd cmd = insert_string(fmt);
						ed_exec_command(cmd);
					}
					ed_exec_command(post);
				} break;
			}
		}

		ivec2 win_size = os_window_size(win);
		Quad window_rect = {
			{ 0, 0 },
			{ (f32) win_size.x, (f32) win_size.y }
		};

		UI_Config frame = {};
		frame.flags = UI_Invisible;
		frame.padding = Pad((u16) THEME.radius);

		gfx_begin();
		ui_begin_frame(window_rect, frame);

		UI_Config panel = {};
		UI_Box *panel_box = nullptr;
		panel.size = { size_fill(1.0), size_fill(1.0) };
		UI(panel) {
			panel_box = __this_box__;
		}

		UI_Config status = {};
		status.flags = UI_Clip_Children;
		status.fill_color = THEME.background_dim;
		status.border_color = THEME.border;
		status.radius = THEME.radius;
		status.size = { size_fill(1.0), size_fit() };
		status.layout = Layout_Row;
		status.padding = {4,8,4,4};
		status.border = 1.0f;

		UI(status) {
			UI_Config mode = {};
			mode.fill_color = THEME.border;
			mode.radius = THEME.radius - 2;
			mode.size = { size_fit(), size_fill(1.0) };
			mode.padding = Pad_XY((u16) THEME.radius, 0);

			UI(mode) {
				UI(label(modal_string(curr_mode), THEME.background));
			}

			string left_string = S("");
			if (ed_active()) {
				left_string = ed_active()->path;
			} else {
				left_string = ed_directory();
			} 
			UI(label(left_string, THEME.foreground, Size_Fill, Align_End));
		}

		ui_end_frame();

		draw_buffer_view(ed_active(), panel_box->rect);

		ui_draw();

		if (ed_mode() == Ed_Mode::Command) {
			frame.padding = Pad_XY(0, (u16) (win_size.y * 0.14f));
			frame.layout = Layout_Row;
			ui_begin_frame(window_rect, frame);

			UI(gap({size_fill(1.0), size_fill(1.0)}));


			UI_Config cmd_line = {};
			cmd_line.flags = UI_Drop_Shadow | UI_Clip_Children;
			cmd_line.size = { size_fill(1.0), size_fit() };
			cmd_line.radius = 10.0f;
			cmd_line.fill_color = THEME.background_dim;
			cmd_line.border_color = THEME.border;
			cmd_line.border = 1.0f;
			cmd_line.padding = Pad(10);
			cmd_line.layout = Layout_Row;

			UI(cmd_line) {
				UI(label(ed_command_string(), THEME.gutter_foreground));
				UI_Config cursor = {};
				cursor.size = { size_fixed(3), size_fill(1) };
				cursor.fill_color = THEME.gutter_foreground;
				UI(cursor);
			}


			UI(gap({size_fill(1.0), size_fill(1.0)}));

			ui_end_frame();
			ui_draw();
		}

		gfx_end();
	}
}


funcdef string
format_user_input(rune codepoint, Ed_Cmd *post)
{
	if (codepoint == '\n') {

		Buffer *buf = ed_active();
		u64 line_index = buffer_line_index_at(buf, buffer_cursor(buf));
		Range_u64 range = buffer_line_range(buf, line_index);
		range.end = Min(range.end, buffer_cursor(buf));

		string line = buffer_slice(buf, frame_arena(), range);

		u64 i = 0;
		for (;i<line.len && is_space(line[i]); ++i)
			;

		rune before = buffer_char_at(ed_active(), buffer_cursor(ed_active()) - 1);
		rune after = buffer_char_at(ed_active(), buffer_cursor(ed_active()));

		string result = string_concat(frame_arena(), S("\n"), line.range(0, i));

		if (char_kind(before) == Char_Open) {
			bool between_pair = (char_get_pair(before) == after);

			if (!between_pair) {
				result = string_concat(frame_arena(), result, S("\t"));
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

				string left = utf8_encode(codepoint, frame_arena());
				string right = utf8_encode(pair, frame_arena());
				return string_concat(frame_arena(), left, right);
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
				string left = utf8_encode(codepoint, frame_arena());
				string right = utf8_encode(pair, frame_arena());
				return string_concat(frame_arena(), left, right);
			}
		}

		return utf8_encode(codepoint, frame_arena());
	}

	return {};
}


funcdef Ed_Cmd
parse_command (slice<string> args)
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
