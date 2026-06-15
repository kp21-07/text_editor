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

		rune input_rune = input.codepoint;

		Ed_Cmd cmd = {};

		if (curr_mode == Ed_Mode::Normal || curr_mode == Ed_Mode::Visual) {
			if      (input_rune == key_map(insert_mode))        cmd = change_mode(Ed_Mode::Insert);
			else if (input_rune == key_map(buffer_search_mode)) cmd = change_mode(Ed_Mode::Buffer_Search);
			else if (input_rune == key_map(visual_mode))        cmd = change_mode(Ed_Mode::Visual);
			else if (input_rune == key_map(command_mode))       cmd = change_mode(Ed_Mode::Command);

			else if (input_rune == key_map(cursor_left))  cmd = move_cursor(Direction::Left, 1);
			else if (input_rune == key_map(cursor_down))  cmd = move_cursor(Direction::Down, 1);
			else if (input_rune == key_map(cursor_up))    cmd = move_cursor(Direction::Up, 1);
			else if (input_rune == key_map(cursor_right)) cmd = move_cursor(Direction::Right, 1);
			
			else if (input_rune == key_map(scroll_down))  cmd = move_cursor(Direction::Down, 12);
			else if (input_rune == key_map(scroll_up))    cmd = move_cursor(Direction::Up, 12);
	
			else if (input_rune == key_map(jump_to_end_of_file)) cmd = jump_to_eof();
			else if (input_rune == key_map(jump_to_start_of_line)) cmd = jump_to_start_of_line();

			else if (input_rune == key_map(increase_font_size)) gfx_set_font_height(gfx_get_font_height() + 2);
			else if (input_rune == key_map(decrease_font_size)) gfx_set_font_height(gfx_get_font_height() - 2);
			else if (input_rune == key_map(paste_text)) cmd = insert_string(os_get_clipboard_string(frame_arena()));

			else if (
				input_rune == key_map(jump_to_end_of_line) ||
				input_rune == key_map(insert_at_end_of_line)
			) {
				cmd = jump_to_end_of_line();
				if (input_rune == key_map(insert_at_end_of_line)) {
					ed_exec_command(cmd);
					cmd = change_mode(Ed_Mode::Insert);
				}
			}
			else if (
				input_rune == key_map(jump_to_first_non_white) ||
				input_rune == key_map(insert_at_first_non_white)
			) {
				cmd = jump_to_first_non_white();
				if (input_rune == key_map(insert_at_first_non_white)) {
					ed_exec_command(cmd);
					cmd = change_mode(Ed_Mode::Insert);
				}
			}
		}
		else if (curr_mode == Ed_Mode::Buffer_Search) {
			if      (input_rune == '\x7F' || input_rune == '\b') cmd = delete_string(Direction::Left, 1);
			else if (input_rune == key_map(autocomplete)) {
				string cmd_string = ed_command_string();
				slice<string> paths = ed_open_buffers();
				paths = fuzzy_filter(paths, cmd_string, frame_arena());
				if (paths.len > 0) {
					ed_exec_command(delete_string(Direction::Left, cmd_string.len));
					cmd = insert_string(paths[0]);
				}
			}
			else if (input_rune == key_map(ui_confirm)) {
				string cmd_string = ed_command_string();
				slice<string> paths = ed_open_buffers();
				paths = fuzzy_filter(paths, cmd_string, frame_arena());

				if (paths.len > 0) {
					cmd = open_buffer(paths.range(0,1));
					ed_exec_command(cmd);
				}

				cmd = change_mode(Ed_Mode::Normal);
			}
			else if (unicode_visual_rune(input_rune)) cmd = insert_string(utf8_encode(input.codepoint, frame_arena()));
		}
		else if (curr_mode == Ed_Mode::Command) {
			if      (input_rune == '\x7F' || input_rune == '\b') cmd = delete_string(Direction::Left, 1);
			else if (input_rune == key_map(ui_confirm)) {
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
			}	
			else if (unicode_visual_rune(input_rune)) cmd = insert_string(utf8_encode(input.codepoint, frame_arena()));
		}
		else if (curr_mode == Ed_Mode::Insert) {
			if      (input_rune == '\x7F') cmd = delete_string(Direction::Right, 1);
			else if (input_rune == '\b')   cmd = delete_string(Direction::Left, 1);
			else if (unicode_visual_rune(input_rune) ||
					input_rune == '\n' ||
					input_rune == '\t') 
			{
				Ed_Cmd post = {};
				string fmt = format_user_input(input.codepoint, &post);
				if (fmt.len) {
					cmd = insert_string(fmt);
					ed_exec_command(cmd);
					cmd = {};
				}
				ed_exec_command(post);
			}
		}
		else if (curr_mode == Ed_Mode::Visual) {
			if   (input_rune == key_map(cursor_left))  cmd = move_cursor(Direction::Left, 1);
			else if (input_rune == key_map(cursor_down))  cmd = move_cursor(Direction::Down, 1);
			else if (input_rune == key_map(cursor_up))    cmd = move_cursor(Direction::Up, 1);
			else if (input_rune == key_map(cursor_right)) cmd = move_cursor(Direction::Right, 1);
			else if (input_rune == key_map(copy_text)) {
				cmd = copy_selected();
				ed_exec_command(cmd);
				cmd = change_mode(Ed_Mode::Normal);
			}
		}
		if (!cmd.kind) {
			if (input_rune == key_map(normal_mode)) cmd = change_mode(Ed_Mode::Normal);
		}
		ed_exec_command(cmd);

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
	status.gap = 8.0;

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
	
	draw_buffer_view(ed_active(), panel_box->rect, true && ed_mode() == Ed_Mode::Visual);

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
