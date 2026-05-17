#include "alloc.cpp"
#include "string.cpp"
#include "buffer.cpp"
#include "graphics.cpp"
#include "platform.cpp"

#include "config.h"

enum Editor_Mode {
	Mode_Normal,
	Mode_Insert,
	Mode_Command,
	Mode_Count,
};

const string MODE_STRING[Mode_Count] = {
	S("normal"),
	S("insert"),
	S("command"),
};

global struct {
	Editor_Mode mode;
	List<Buffer> buffers;
	List<u8> cmd_buffer;
	vec2 cursor_pos;
	f32 delta_time;
} editor;

funcdef void
draw_buffer_to_rect(Buffer *buffer, Rect region, Arena *transient)
{
	draw_quad(region.from, region.size, Color::bg);

	graphics_push_clip(region, transient);

	local_persist f32 space_width = graphics_char_width(' ');

	f32 y = region.from.y;

	f32 region_width = region.size.x;
	f32 region_height = region.size.y;

	Slice<string> lines = buffer_as_lines(buffer, transient);

	for (u64 i=0; i<lines.len; ++i) {
		string line = lines[i];

		u64 begin = (u64) (line.raw - buffer->data.raw);
		u64 end   = begin + line.len;

		bool on_cursor = Range_Check(begin, buffer->cursor, end);

		if (on_cursor) { // draw cursor line
			draw_quad({0.0f, y}, { region_width, gfx.line_height}, Color::bg_alt);
			draw_quad({0.0f, y + 2}, { region_width, gfx.line_height - 4}, Color::bg);
		}

		string number_string = string_format(transient, "%zu", i + 1);
		draw_text(number_string, { region.from.x, y }, on_cursor ? Color::accent : Color::dim);

		f32 x = region.from.x + graphics_char_width('0') * (1 + Max(digit_count_u64(lines.len), 2));
		x = floorf(x);

		if (on_cursor && editor.mode != Mode_Command) {
			u64 cursor_offset = buffer->cursor - begin;
			string before_cursor = slice(line, 0, cursor_offset);
			f32 cursor_x = x + graphics_measure_text(before_cursor).x;

			editor.cursor_pos.x = smooth_move(editor.cursor_pos.x, cursor_x, 45, editor.delta_time);
			editor.cursor_pos.y = smooth_move(editor.cursor_pos.y, y, 45, editor.delta_time);

			if (editor.mode != Mode_Insert) {
				draw_capsule(editor.cursor_pos, {space_width, gfx.line_height}, Color::cursor);
				draw_capsule({editor.cursor_pos.x + 1, editor.cursor_pos.y + 1}, {space_width - 2, gfx.line_height - 2}, Color::bg);
			} else {
				draw_quad(editor.cursor_pos, {2, gfx.line_height}, Color::cursor);
			}
		}

		draw_text(line, {x, y}, Color::fg);

		y += gfx.line_height;
		if (y > region.from.y + region.size.y) break;
	}
	
	graphics_pop_clip();
}

int main()
{
	Arena persist = {};
	arena_make(&persist, malloc_bytes(MB(32)));
    
	Arena transient = {};
	arena_make(&transient, malloc_bytes(MB(32)));
    
	graphics_init("</>", 1280, 800, &persist);
    
	{
		auto buf = alloc_slice(&persist, Buffer, 16);
		editor.buffers = list_from_buffer(buf);
		append(&editor.buffers, {});
        
		auto cmd_buf = alloc_slice(&persist, u8, 128);
		editor.cmd_buffer = list_from_buffer(cmd_buf);
	}
    
	auto buf = &editor.buffers[0];
    
	buffer_make(
		buf,
		alloc_slice(&persist, u8, KB(512)),
		alloc_slice(&persist, Line, 2048)
	);

	Frame_Input input = {};
	u64 last_frame_time = platform_time_now();
	while(graphics_update(Hex(0x000000FF), &input))
	{
		u64 curr_time = platform_time_now();
		editor.delta_time = (f32) platform_time_diff(last_frame_time, curr_time).seconds;
		last_frame_time = curr_time;

		rune c = input.character;

		graphics_push_clip({ {0, 0}, {(f32) gfx.win->w , (f32) gfx.win->h }}, &transient);
        
		switch (editor.mode) {
			case Mode_Normal: {
				if (c) {
					switch(c) {
						case 'i': editor.mode = Mode_Insert; break;
						case ':': editor.mode = Mode_Command; break;
						case 'h': buffer_move_cursor(buf, 1, Direction_Left); break;
						case 'l': buffer_move_cursor(buf, 1, Direction_Right); break;
						case 'j': buffer_move_cursor(buf, 1, Direction_Down); break;
						case 'k': buffer_move_cursor(buf, 1, Direction_Up); break;
					}
				}
			} break;
            
			case Mode_Insert: {
				if (input.key_flags & key_Escape) {
					editor.mode = Mode_Normal;
				}
				else if (input.key_flags & key_Delete) {
					buffer_delete(buf, 1);
				}
				else if (input.key_flags & key_Backspace) {
					if (buf->cursor > 0) {
						buffer_move_cursor(buf, 1, Direction_Left);
						buffer_delete(buf, 1);
					}
				}
				else if (c) {
					string input = {
						.raw = (const u8 *)&c,
						.len = 1
					};
					buffer_insert(buf, input);
				}
			} break;
            
			case Mode_Command: {
				if (input.key_flags & key_Escape) {
					editor.mode = Mode_Normal;
					clear(&editor.cmd_buffer);
				}
				else if(input.key_flags & (key_Backspace | key_Delete)) {
					if (editor.cmd_buffer.len > 0)
						editor.cmd_buffer.len -= 1;
				}
				else if(c) {
					if (c == '\n') {
						string cmd_string = {
							.raw = editor.cmd_buffer.raw,
							.len = editor.cmd_buffer.len
						};

						printf("%.*s\n", s_fmt(cmd_string));
						editor.mode = Mode_Normal;
						clear(&editor.cmd_buffer);
						break;
					}
                    
					append(&editor.cmd_buffer, (u8) c);
				}
			} break;
            
			case Mode_Count: {
			} break;
		}
      
		draw_buffer_to_rect(buf, {{0, 0}, {(f32)gfx.win->w, (f32)gfx.win->h - gfx.line_height}}, &transient);

		draw_quad({0, gfx.win->h - gfx.line_height}, {(f32) gfx.win->w, gfx.line_height}, Color::bg_alt);

		string mode_string = string_format(&transient, "-- %.*s --", s_fmt(MODE_STRING[editor.mode]));
		draw_text(mode_string, {0, gfx.win->h - gfx.line_height}, Color::accent);

		if (editor.mode == Mode_Command) {
			const f32 panel_width = gfx.win->w * 0.5f;
			const f32 panel_height = gfx.win->h * 0.5f;

			f32 x0 = (f32) (gfx.win->w - panel_width) * 0.5f;
			f32 y0 = (f32) (gfx.win->h - panel_height) * 0.5f;

			draw_quad_rounded({x0, y0}, {panel_width, panel_height}, 10, Color::accent);
			draw_quad_rounded({x0 + 1, y0 + 1}, {panel_width - 2, panel_height - 2}, 9, Color::bg);

			draw_quad({x0, y0 + 8 + gfx.line_height}, {panel_width, 1}, Color::accent);
	
			string cmd_string = {
				.raw = editor.cmd_buffer.raw,
				.len = editor.cmd_buffer.len
			};

			vec2 cursor_pos = {x0 + 4, y0 + 5};
			vec2 size = draw_text(cmd_string, cursor_pos, Color::accent);
			cursor_pos.x += size.x;

			editor.cursor_pos.x = smooth_move(editor.cursor_pos.x, cursor_pos.x, 45, editor.delta_time);
			editor.cursor_pos.y = smooth_move(editor.cursor_pos.y, cursor_pos.y, 45, editor.delta_time);

			draw_capsule(editor.cursor_pos, {(f32) graphics_char_width(' '), gfx.line_height}, Color::accent);
		}
        
		arena_free(&transient);
	}
}
