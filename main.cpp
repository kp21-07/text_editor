#include "editor.h"
#include "config.h"

funcdef string format_user_input(rune codepoint, Ed_Cmd *post);
funcdef Ed_Cmd parse_command (slice<string> args);

funcdef void layout_editor_ui(Quad window);
funcdef void layout_command_ui(Quad window);
funcdef void layout_buffer_search_ui(Quad window);

void entry_point(slice<string> args)
{
	ed_init();
	defer(ed_deinit());

	ed_exec_command(open_buffer(args.range(1, args.len)));
    
	OS_Handle win = os_open_window(persist_arena(), S("editor"));
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
				case ':':  cmd = change_mode(Ed_Mode::Command); break;
				case 'i':  cmd = change_mode(Ed_Mode::Insert); break;
				case '\t': cmd = change_mode(Ed_Mode::Buffer_Search); break;
				case 'v':  cmd = change_mode(Ed_Mode::Visual); break;

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
				} break;

				case '-': {
					f32 curr_height = gfx_get_font_height();
					gfx_set_font_height(curr_height - 2);
				} break;

				case '+' : {
					f32 curr_height = gfx_get_font_height();
					gfx_set_font_height(curr_height + 2);
				} break;
			}

			ed_exec_command(cmd);
		} else if (curr_mode == Ed_Mode::Buffer_Search) {
			switch (input.codepoint) {
			case '\x1b': {
				ed_exec_command(change_mode(Ed_Mode::Normal));
			} break;

			case '\x7F': case '\b': { // delete
				Ed_Cmd cmd = delete_string(Direction::Left, 1);
				ed_exec_command(cmd);
			} break;

			case '\t': {
				string cmd_string = ed_command_string();
				slice<string> paths = ed_open_buffers();
				paths = fuzzy_filter(paths, cmd_string, frame_arena());

				if (paths.len > 0) {
					Ed_Cmd cmd = delete_string(Direction::Left, cmd_string.len);
					ed_exec_command(cmd);

					cmd = insert_string(paths[0]);
					ed_exec_command(cmd);
				}
			} break;
			case '\n': {
				Ed_Cmd cmd = {};

				string cmd_string = ed_command_string();
				slice<string> paths = ed_open_buffers();
				paths = fuzzy_filter(paths, cmd_string, frame_arena());

				if (paths.len > 0) {
					cmd = open_buffer(paths.range(0,1));
					ed_exec_command(cmd);
				}

				cmd = change_mode(Ed_Mode::Normal);
				ed_exec_command(cmd);
			} break;

			default: {
				if (!unicode_visual_rune(input.codepoint))
					break;

				Ed_Cmd cmd = insert_string(
					utf8_encode(input.codepoint, frame_arena())
				);
				ed_exec_command(cmd);
			} break;
			}
		} else if (curr_mode == Ed_Mode::Command) {
			switch (input.codepoint) {
				case '\x1b': {
					Ed_Cmd cmd = change_mode(Ed_Mode::Normal);
					ed_exec_command(cmd);
				} break;

				case '\x7F': case '\b': { // delete
					Ed_Cmd cmd = delete_string(Direction::Left, 1);
					ed_exec_command(cmd);
				} break;

				case '\n': {
					Temp t0 = temp_begin(scratch(0, 0));
					defer(temp_end(t0));

					slice<string> args = ed_command_strings(t0.arena);

					Ed_Cmd cmd = parse_command(args);
					ed_exec_command(cmd);
					if (cmd.kind == Cmd_Reload) {
						gfx_set_font_height(cfg_f32(font_height));
					}

					cmd = change_mode(Ed_Mode::Normal);
					ed_exec_command(cmd);
				} break;

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
		} else if (curr_mode == Ed_Mode::Visual) {
			switch (input.codepoint) {
				case '\x1b': {
					Ed_Cmd cmd = change_mode(Ed_Mode::Normal);
					ed_exec_command(cmd);
				} break;
			}
		}

		ivec2 win_size = os_window_size(win);
		Quad window_rect = {
			{ 0, 0 },
			{ (f32) win_size.x, (f32) win_size.y }
		};


		gfx_begin();
	
		layout_editor_ui(window_rect);

		switch (ed_mode()) {
		case Ed_Mode::Command:
			layout_command_ui(window_rect);
			break;
		case Ed_Mode::Buffer_Search:
			layout_buffer_search_ui(window_rect);
			break;
		default:
			break;
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
	s64 i_val = string_to_s32(main_arg, &int_ok);

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
	else if (string_equal(main_arg, cmd_function(Cmd_Reload))) {
		Ed_Cmd cmd = reload_config();
		return cmd;
	}

	return {};
}


funcdef void
layout_editor_ui(Quad window)
{
	UI_Config frame = {};
	frame.flags = UI_Invisible;
	frame.padding = Pad((u16) 4);

	ui_begin_frame(window, frame);
	UI_Box *panel_box;

	//
	// panel view
	//

	UI_Config panel = {};
	panel.size = {size_fill(1), size_fill(1)};

	UI(panel) {
		panel_box = __this_box__;
	}

	//
	// status line
	//

	UI_Config status = {};
	status.flags = UI_Clip_Children;
	status.fill_color = cfg_color(status_line);
	status.radius = cfg_f32(radius);
	status.size = {size_fill(1.0), size_fit()};
	status.layout = Layout_Row;
	status.padding = Pad(4);
	status.gap = 4.0;

	Ed_Mode mode = ed_mode();

	UI(status) {
		UI(fit_container(cfg_color(accent), Pad_XY(4, 0), cfg_f32(radius) - 4)) {
			UI(label(modal_string(mode), cfg_color(background)));
		}
		
		UI(gap({size_fill(1.0), size_fill(1.0)}));

		if (ed_active()) {
			UI(label(ed_active()->path, cfg_color(accent)));
			UI(fit_container(cfg_color(accent), Pad_XY(4, 0), cfg_f32(radius) - 4)) {
				UI(label(file_kind_string(ed_active()->file_kind), cfg_color(background)));
			}
		} else {
			UI(fit_container(cfg_color(border), Pad_XY(4, 0), cfg_f32(radius) - 4))
				UI(label(ed_directory(), cfg_color(foreground)));
		}
	}

	ui_end_frame();
	
	draw_buffer_view(ed_active(), panel_box->rect);

	ui_draw();
}

funcdef void
layout_command_ui(Quad window)
{
	UI_Config frame = {};
	frame.flags = UI_Invisible;
	frame.padding = Pad_XY(0, (u16) (window.size.y * 0.14f));
	frame.layout = Layout_Row;

	ui_begin_frame(window, frame);

	UI(gap({size_fill(0.6f), size_fill(1.0)}));

	//
	// actual command line box
	//

	UI_Config cmd_line = {};
	cmd_line.flags = UI_Drop_Shadow | UI_Clip_Children;
	cmd_line.size = { size_fill(1.0), size_fit() };
	cmd_line.radius = cfg_f32(radius);
	cmd_line.fill_color = cfg_color(background_dim);
	cmd_line.border_color = cfg_color(border);
	cmd_line.border = 1.0f;
	cmd_line.padding = Pad(10);
	cmd_line.layout = Layout_Row;

	UI(cmd_line) {
		UI(label(ed_command_string(), cfg_color(foreground)));

		UI_Config cursor = {};
		cursor.size = { size_fixed(2), size_fill(1) };
		cursor.fill_color = cfg_color(cursor);
		UI(cursor);
	}

	UI(gap({size_fill(0.6f), size_fill(1.0)}));

	ui_end_frame();
	ui_draw();
}


funcdef void
layout_buffer_search_ui(Quad window)
{
	UI_Config frame = {};
	frame.flags = UI_Invisible;
	frame.padding = Pad_XY(0, (u16) (window.size.y * 0.14f));
	frame.layout = Layout_Row;

	ui_begin_frame(window, frame);

	UI(gap({size_fill(0.6), size_fill(1.0)}));

	//
	// main buffer search ui
	//

	UI_Config search_panel = {};
	search_panel.flags = UI_Drop_Shadow | UI_Clip_Children;
	search_panel.size = { size_fill(1.0), size_fill(1.0) };
	search_panel.radius = cfg_f32(radius);
	search_panel.fill_color = cfg_color(background_dim);
	search_panel.border_color = cfg_color(border);
	search_panel.border = 1.0f;
	search_panel.padding = Pad(10);
	search_panel.layout = Layout_Col;
	search_panel.gap = 4;

	UI(search_panel) {
		UI_Config search_box = {};
		search_box.flags = UI_Invisible;
		search_box.size = { size_fill(1), size_fit() };
		search_box.layout = Layout_Row;

		string cmd_string = ed_command_string();

		slice<string> paths = ed_open_buffers();
		paths = fuzzy_filter(paths, cmd_string, frame_arena());

		//
		// search box
		//

		UI(search_box) {
			UI(label(cmd_string, cfg_color(foreground)));

			UI_Config cursor = {};
			cursor.size = { size_fixed(2), size_fill(1) };
			cursor.fill_color = cfg_color(cursor);

			UI(cursor);

			if (paths.len > 0 && cmd_string.len > 0) {
				UI(label(paths[0].range(cmd_string.len, paths[0].len), cfg_color(status_line)));
			}
		}

		//
		// results
		//

		for (u64 i=0; i<paths.len; ++i) {
			string path_str = paths[i];
			UI(label(path_str, cfg_color(gutter_foreground)));
		}
	}

	UI(gap({size_fill(0.6), size_fill(1.0)}));

	ui_end_frame();
	ui_draw();
}