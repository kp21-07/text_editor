#include "alloc.cpp"
#include "editor.h"
#include "string.cpp"
#include "buffer.cpp"
#include "graphics.cpp"
#include "platform.cpp"
#include "editor.cpp"

#include "config.h"

funcdef void
draw_buffer_view(Buffer *buffer, Rect region)
{
	draw_quad(region.from, region.size, Color::bg);

	if (!buffer) {
		string cwd = editor.project_dir;
		string msg = S("-- no file open --");

		vec2 size = graphics_measure_text(msg);

		vec2 p = {
			region.from.x + (region.size.x - size.x) * 0.5f,
			region.from.y + (region.size.y - size.y) * 0.5f,
		};

		draw_text(msg, p, Color::error);

		size = graphics_measure_text(cwd);

		p = {
			region.from.x + (region.size.x - size.x) * 0.5f,
			region.from.y + (region.size.y - size.y) * 0.5f + graphics_line_height(),
		};

		draw_text(cwd, p, Color::error);

		return;
	}

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

			vec2 cursor_pos = {cursor_x, y};

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

	//
	// status line
	//

	vec2 status_pos = { region.from.x, region.from.y + region.size.y - line_height };

	draw_quad_rounded(status_pos, {region.size.x, line_height}, 5, Color::dim);

	string mode_string = string_format(
		editor.frame_arena,
		"-- %.*s --",
		s_fmt(MODE_STRING[editor.mode])
	);

	string path = string_format(ed_frame_arena(), "%c%.*s", buffer->dirty ? '*': ' ', s_fmt(buffer->path));
	vec2 path_size = graphics_measure_text(path);

	draw_text(
		mode_string,
		{status_pos.x + 5, status_pos.y},
		Color::bg
	);

	draw_text(
		path,
		{ region.from.x + region.size.x - path_size.x - 5, status_pos.y },
		Color::bg
	);
}

int main(int argc, char **argv)
{
	ed_init();
	defer(ed_deinit());
	
	graphics_init("text editor", 1280, 800, ed_persist_arnea());
	defer(graphics_deinit());

	u64 last_frame_time = platform_time_now();
	f32 delta_time = 0;

	Slice<string> args = string_list((u8 **) argv, (u64) argc, ed_frame_arena());
	if (args.len > 1) {
		string path = args[1];
		if (platform_is_dir(path)) ed_execute_cmd(open_workspace(path));
		else open_buffer(path);
	}

	for (bool quit = false; !quit;)
	{
		u64 curr_time = platform_time_now();
		delta_time = (f32) platform_time_diff(last_frame_time, curr_time).seconds;
		last_frame_time = curr_time;

		Frame_Input input = {};

		bool window_close = graphics_update(&input);
		quit = ed_update(input);

		if (window_close) break;

		Rect window_rect = {};
		window_rect.size = graphics_resolution();

		graphics_push_clip(window_rect, ed_frame_arena());
		defer(graphics_pop_clip());

		draw_buffer_view(ed_active_buffer(), window_rect);

		if (ed_mode() == Mode_Command) {
			push_draw_layer_scoped(Draw_Layer_Popup) {
				const f32 panel_width = window_rect.size.x * 0.3f;
				const f32 panel_height = graphics_line_height();

				f32 x0 = (f32) (window_rect.size.x - panel_width) * 0.5f;
				f32 y0 = 32.0f;

				draw_quad_rounded({x0 - 2, y0 - 2}, {panel_width + 4, panel_height + 4}, 5, Color::cursor);
				draw_quad_rounded({x0 - 1, y0 - 1}, {panel_width + 2, panel_height + 2}, 5, Color::bg);

				Rect clip = {
					{x0, y0}, {panel_width, panel_height}
				};

				graphics_push_clip(clip, ed_frame_arena());
				defer(graphics_pop_clip());

				string str = ed_command_as_string();

				vec2 text_pos = {x0 + 5, y0};
				vec2 size = draw_text(str, text_pos, Color::cursor);
				text_pos.x += size.x;

				draw_quad(text_pos,  {2, graphics_line_height()}, Color::cursor);
			}
		}

		graphics_submit_draw();

		arena_free(ed_frame_arena());
	}

}
