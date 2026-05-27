#include "alloc.cpp"
#include "editor.h"
#include "string.cpp"
#include "buffer.cpp"
#include "graphics.cpp"
#include "platform.cpp"
#include "editor.cpp"
#include "ui.cpp"

#include "config.h"

funcdef void
draw_buffer_view(Buffer *buffer, Rect region)
{
	local_persist vec2 visual_cursor = {};
	local_persist bool initialized = false;

	graphics_push_clip(region, ed_frame_arena());
	defer(graphics_pop_clip());

	f32 line_height = graphics_line_height();
	f32 digit_width = graphics_char_width('0');
	f32 space_width = graphics_char_width(' ');

	Slice<string> lines = buffer_as_lines(buffer, ed_frame_arena());

	u64 cursor_line = buffer_line_at_index(buffer, buffer->cursor);
	Range_U64 cursor_range = buffer_line_range(buffer, cursor_line);

	//
	// gutter
	//

	u64 gutter_digits = Max(digit_count_u64(lines.len), 2);

	f32 gutter_pad   = digit_width;
	f32 gutter_width = gutter_pad * 2 + digit_width * gutter_digits;

	f32 text_x = region.from.x + gutter_width;

	//
	// draw lines
	//

	f32 y = region.from.y;

	for (u64 i = 0; i < lines.len; ++i) {
		if (y > region.from.y + region.size.y) {
			break;
		}

		string line = lines[i];

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

			static vec2 visual_cursor = {};
			static bool initialized = false;

			vec2 target_cursor = {cursor_x, y};

			if (!initialized) {
				visual_cursor = target_cursor;
				initialized = true;
			}

			f32 dt = graphics_delta_time();
			f32 speed = 40.0f;

			f32 t = 1.0f - expf(-speed * dt);

			visual_cursor.x += (target_cursor.x - visual_cursor.x) * t;
			visual_cursor.y += (target_cursor.y - visual_cursor.y) * t;

			vec2 cursor_pos = visual_cursor;

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
		.flags = UI_Invisible,
		.size = {
			{ Size_Fill, 1.0f },
			{ Size_Fill, 1.0f }
		},
	) {
		auto buf = ed_active_buffer();
		if (!buf) {
			string directory = editor.project_directory;

			UI(
				.size = {
					{ Size_Fill, 1.0f },
					{ Size_Fill, 1.0f }
				},
				.color = Color::error,
				.text = directory,
				.align = Align_Center,
			);
			break;
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

int main(int argc, char **argv)
{
	ed_init();
	ui_init(ed_frame_arena());
	graphics_init("text editor", 1280, 800, ed_persist_arena());

	u64 last_frame_time = platform_time_now();

	Slice<string> args = string_list((u8 **) argv, (u64) argc, ed_frame_arena());
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
			ui_begin_frame(window_rect);
			UI_Box *panel_box = layout_panel_ui();
			auto draw_list = ui_end_frame();
			ui_draw_cmd_list(draw_list);

			if (panel_box) {
				draw_buffer_view(ed_active_buffer(), panel_box->rect);
			}
		}

		if (ed_mode() == Mode_Command) {
			const f32 status_height = graphics_line_height() + 4;
			ui_begin_frame(window_rect, UI_Invisible, Layout_Row, Pad_XY(0, 100));

			string cmd_string = ed_command_as_string();
			cmd_string = string_format(ed_frame_arena(), ":%.*s", s_fmt(cmd_string));
			UI(
				.flags = UI_Invisible,
				.size = {
					{Size_Fill, 1.0},
					{Size_Fill, 1.0},
				},
			);
			UI(
				.size = {
					{ Size_Fill, 2.0f },
					{ Size_Fixed, status_height }
				},
				.padding = Pad(2),
				.radius = 5,
				.border = 1.0f,
				.color = Color::bg,
				.border_color = Color::cursor,
			) {
				UI(
					.size = {
						{ Size_Fill, 1.0f },
						{ Size_Fill, 1.0f }
					},
					.color = Color::fg,
					.text = cmd_string,
				);
			}
			UI(
				.flags = UI_Invisible,
				.size = {
					{Size_Fill, 1.0},
					{Size_Fill, 1.0},
				}
			);

			auto draw_list = ui_end_frame();
			ui_draw_cmd_list(draw_list);
		}

		graphics_submit_draw();

		arena_free(ed_frame_arena());
	}

	graphics_deinit();
	ed_deinit();
}
