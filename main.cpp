#include "editor.h"
#include "config.h"

const string MODE_STRING[Mode_Count] = {
	S("normal"),
	S("insert"),
	S("command"),
	S("buffer search")
};

funcdef void
draw_buffer_view(Buffer *buffer, Rect region)
{
	graphics_push_clip(region, ed_frame_arena());
	defer(graphics_pop_clip());

	f32 line_height = graphics_line_height();
	f32 digit_width = graphics_char_width('0');
	f32 space_width = graphics_char_width(' ');

	string buf_string = string_from_bytes(slice_from_list(buffer->data));
	Slice<Line> buffer_lines = slice_from_list(buffer->lines);


	u64 cursor_line = buffer_line_at_index(buffer, buffer->cursor);
	Range_U64 cursor_range = buffer_line_range(buffer, cursor_line);

	//
	// gutter
	//

	u64 gutter_digits = Max(digit_count_u64(buffer_lines.len), 2);

	f32 gutter_pad   = digit_width;
	f32 gutter_width = gutter_pad * 2 + digit_width * gutter_digits;

	f32 text_x = region.from.x + gutter_width;

	//
	// draw lines
	//

	f32 y = region.from.y;

	for (u64 i = 0; i < buffer_lines.len; ++i) {
		if (y > region.from.y + region.size.y) {
			break;
		}

		Range_U64 range = buffer_line_range(buffer, i);
		string line = slice(buf_string, range.begin, range.end);

		bool current_line = (i == cursor_line);

		if (current_line) {
			draw_quad_rounded(
				{text_x, y},
				{region.size.x - gutter_width - digit_width, line_height},
				5,
				Color::bg_alt
			);
		}

		string line_number = string_format(
			ed_frame_arena(),
			"%*zu",
			(int)gutter_digits,
			i + 1
		);

		if(current_line) {
			draw_quad_rounded(
				{region.from.x + gutter_pad, y},
				{digit_width * gutter_digits, line_height},
				5,
				Color::dim
			);
		}

		draw_text(
			line_number,
			{region.from.x + gutter_pad, y},
			current_line ? Color::bg : Color::dim
		);

		draw_text(line, {text_x, y}, Color::fg);

		if (current_line && ed_mode() != Mode_Command) {
			u64 cursor_offset = buffer->cursor - cursor_range.begin;

			string before_cursor = slice(line, 0, cursor_offset);

			f32 cursor_x = text_x + graphics_measure_text(before_cursor).x;
			vec2 target_cursor = {cursor_x, y};
			vec2 cursor_pos = target_cursor;

			if (ed_mode() != Mode_Insert) {
				draw_capsule(
					cursor_pos,
					{space_width, line_height},
					Color::cursor
				);

				if (cursor_offset < line.len) {
					s32 width = 0;

					utf8_decode(slice(line, cursor_offset, line.len), &width);

					string cursor_char = slice(line, cursor_offset, cursor_offset + width);

					draw_text(cursor_char, cursor_pos, Color::bg);
				}
			} else {
				draw_quad(
					cursor_pos,
					{2, line_height},
					Color::cursor
				);
			}
		}
		y += line_height;
	}
}

funcdef UI_Box *
layout_panel_ui() {
	const f32 status_height = graphics_line_height() + 4;

	UI_Box *panel_box = nullptr;

	UI(
		.size = {
			{ Size_Fill, 1.0f },
			{ Size_Fill, 1.0f }
		},
		.color = Color::bg
	) {
		auto buf = ed_active_buffer();
		if (!buf) {
			string directory = ed_project_dir();

			UI(
				.size = {
					{ Size_Fill, 1.0f },
					{ Size_Fill, 1.0f }
				},
				.color = Color::error,
				.text = directory,
				.align = Align_Center,
			);
			continue;
		}

		panel_box = ui_current();
	}

	UI(
		.size = {
			{ Size_Fill, 1.0f },
			{ Size_Fixed, status_height }
		},
		.padding = Pad(2),
		.radius = 5,
		.color = Color::dim,
		.layout = Layout_Row,
	) {
		string mode_string = MODE_STRING[ed_mode()];
		UI(
			.size = {
				{ Size_Fill, 1.0f },
				{ Size_Fill, 1.0f }
			},
			.color = Color::bg,
			.text = mode_string,
		);

		auto buf = ed_active_buffer();
		if (buf) {
			f32 width = graphics_measure_text(buf->path).x;
			UI(
				.size = {
					{ Size_Fixed, width },
					{ Size_Fill, 1.0f }
				},
				.color = Color::bg,
				.text = buf->path,
			);
		}
	}

	return panel_box;
}

void entry_point(Slice<string> args)
{
	ed_init();
	ui_init(ed_frame_arena());
	graphics_init("text editor", 1280, 800, ed_persist_arena());

	u64 last_frame_time = platform_time_now();

	if (args.len > 1) {
		string path = args[1];

		if (platform_is_directory(path, ed_frame_arena()))
			ed_execute_cmd(open_workspace(path));
		else
			ed_execute_cmd(open_buffer(path));
	}

	for (bool quit = false; !quit;)
	{
		Frame_Input input = {};

		bool window_close = graphics_update(&input);
		quit = ed_update(input);

		if (window_close) break;

		Rect window_rect = {};
		window_rect.size = graphics_resolution();

		graphics_push_clip(window_rect, ed_frame_arena());
		defer(graphics_pop_clip());

		{
			ui_begin_frame(window_rect, 0, Layout_Row);

			UI_Box *panel  = nullptr;

			UI(
				.flags = UI_Invisible,
				.size = {
					{ Size_Fill, 1.0f },
					{ Size_Fill, 1.0f }
				},
				.layout = Layout_Col,
			) {
				panel = layout_panel_ui();
			}

			auto draw_list = ui_end_frame();

			ui_draw_cmd_list(draw_list);

			if (panel) {
				draw_buffer_view(ed_active_buffer(), panel->rect);
			}
		}

		if (ed_mode() == Mode_Command) {
			ui_begin_frame(window_rect, 0, Layout_Row, Pad_XY(0, 100), Hex(0x00000066));
			const f32 status_height = graphics_line_height() + 4;

			string cmd_string = ed_command_as_string();
			cmd_string = string_format(ed_frame_arena(), ":%.*s", s_fmt(cmd_string));
			UI( .flags = UI_Invisible, .size = { {Size_Fill, 1.0}, {Size_Fill, 1.0}, },);
			UI(
				.size = {
					{ Size_Fill, 1.0f },
					{ Size_Fixed, status_height }
				},
				.padding = Pad(2),
				.radius = 5,
				.border = 1.0f,
				.color = Color::bg,
				.border_color = Color::cursor,
				.layout = Layout_Row,
			) {
				f32 width = graphics_measure_text(cmd_string).x;
				UI(
					.size = {
						{ Size_Fixed, width },
						{ Size_Fill, 1.0f }
					},
					.color = Color::fg,
					.text = cmd_string,
				);
				UI(
					.size = {
						{ Size_Fixed, 2 },
						{ Size_Fill,  1.0f },
					},
					.color = Color::cursor,
				);
			}
			UI( .flags = UI_Invisible, .size = { {Size_Fill, 1.0}, {Size_Fill, 1.0}, },);

			auto draw_list = ui_end_frame();
			ui_draw_cmd_list(draw_list);
		} else if (ed_mode() == Mode_Buffer_Search) {
			ui_begin_frame(window_rect, 0, Layout_Row, Pad_XY(0, 100), Hex(0x00000066));

			UI( .flags = UI_Invisible, .size = { {Size_Fill, 1.0}, {Size_Fill, 1.0}, },);
			UI(
				.size = {
					{ Size_Fill, 1.0f },
					{ Size_Fill, 1.0f }
				},
				.padding = Pad(5),
				.radius = 10,
				.border = 1.0f,
				.color = Color::bg,
				.border_color = Color::cursor,
				.layout = Layout_Col,
			) {
				Buffer *buf = ed_buffer_list();
				for(;buf; buf = buf->next) {
					UI(
						.size = {
							{ Size_Fill, 1.0f },
							{ Size_Fixed, graphics_line_height() }
						},
						.color = Color::accent,
						.text = buf->path,
					);
				}
			}
			UI( .flags = UI_Invisible, .size = { {Size_Fill, 1.0}, {Size_Fill, 1.0}, },);

			auto draw_list = ui_end_frame();
			ui_draw_cmd_list(draw_list);
		}

		graphics_submit_draw();

		arena_free(ed_frame_arena());
	}

	graphics_deinit();
	ed_deinit();
}
