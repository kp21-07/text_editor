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
	Arena *persist;
	Arena *transient;

	Editor_Mode mode;

	List<Buffer> buffers;

	vec2 cursor_pos;

	f32 delta_time;
	f32 time;
} editor;

funcdef string
command_palette_interface(Frame_Input input)
{
	u8 input_char = (u8) input.character;
	local_persist u8 cmd_buf[128];
	local_persist List<u8> cmd_string = {
		cmd_buf, 0, sizeof(cmd_buf)
	};

	u32 key_flags = input.key_flags;

	if (key_flags & key_Escape) {
		editor.mode = Mode_Normal;
		clear(&cmd_string);
		return {0};
	}
	else if (key_flags & (key_Backspace | key_Delete)) {
		if (cmd_string.len > 0) cmd_string.len -= 1;	
	} else if (input_char) {
		if (input_char == '\n') {
			bytes result = slice_from_list(cmd_string);
			printf("%.*s\n", s_fmt(result));
			editor.mode = Mode_Normal;
			clear(&cmd_string);

			return string { result.raw, result.len };
		}
		else if (input_char == '\t') {
			// TODO: autocompletion
		}

		if (cmd_string.len < cmd_string.capacity)
			append(&cmd_string, input_char);
	}

	/////////////////////////////////////////////////
	// ~geb: render	

	push_draw_layer_scoped(Draw_Layer_Popup) {
		const f32 panel_width = gfx.win->w * 0.3f;
		const f32 panel_height = gfx.line_height;

		f32 x0 = (f32) (gfx.win->w - panel_width) * 0.5f;
		f32 y0 = 32.0f;

		draw_quad_rounded({x0 - 2, y0 - 2}, {panel_width + 4, panel_height + 4}, 5, Color::cursor);
		draw_quad_rounded({x0 - 1, y0 - 1}, {panel_width + 2, panel_height + 2}, 5, Color::bg);

		Rect clip = {
			{x0, y0}, {panel_width, panel_height}
		};

		graphics_push_clip(clip, editor.transient);
		defer(graphics_pop_clip());

		string str = {
			.raw = cmd_string.raw,
			.len = cmd_string.len
		};

		str = string_format(editor.transient, ":%.*s", s_fmt(cmd_string));

		vec2 text_pos = {x0, y0};
		vec2 size = draw_text(str, text_pos, Color::cursor);
		text_pos.x += size.x;

		draw_quad(text_pos,  {2, gfx.line_height}, Color::cursor);
	}

	return {0};

	u8 layer;
}

funcdef void
draw_buffer_view(Buffer *buffer, Rect region) 
{
	local_persist f32 space_width = graphics_char_width(' ');


	draw_quad(region.from, region.size, Color::bg);

	graphics_push_clip(region, editor.transient);
	defer(graphics_pop_clip());

	f32 size_x = region.size.x;
	f32 size_y = region.size.y;

	Slice<string> lines = buffer_as_lines(buffer, editor.transient);

	for(u64 i=0; i<lines.len; ++i) {
		string line = lines[i];
	
	}

	
}

funcdef void
draw_buffer_to_rect(Buffer *buffer, Rect region) {
	draw_quad(region.from, region.size, Color::bg);

	graphics_push_clip(region, editor.transient);
	defer(graphics_pop_clip());

	local_persist f32 space_width = graphics_char_width(' ');

	f32 y = region.from.y;

	f32 region_width = region.size.x;
	f32 region_height = region.size.y;

	Slice<string> lines = buffer_as_lines(buffer, editor.transient);

	for (u64 i=0; i<lines.len; ++i) {
		string line = lines[i];

		u64 begin = (u64) (line.raw - buffer->data.raw);
		u64 end   = begin + line.len;

		bool on_cursor = Range_Check(begin, buffer->cursor, end);

		if (on_cursor) { // draw cursor line
			draw_quad({region.from.x, y}, { region_width, gfx.line_height}, Color::bg_alt);
			draw_quad({region.from.x, y + 2}, { region_width, gfx.line_height - 4}, Color::bg);
		}

		string number_string = string_format(editor.transient, "%zu", i + 1);
		draw_text(number_string, { region.from.x, y }, on_cursor ? Color::accent : Color::dim);

		f32 x = region.from.x + graphics_char_width('0') * (1 + Max(digit_count_u64(lines.len), 2));
		x = floorf(x);


		draw_text(line, {x, y}, Color::fg);

		if (on_cursor && editor.mode != Mode_Command) {
			u64 cursor_offset = buffer->cursor - begin;
			string before_cursor = slice(line, 0, cursor_offset);
			f32 cursor_x = x + graphics_measure_text(before_cursor).x;

			editor.cursor_pos.x = smooth_move(editor.cursor_pos.x, cursor_x, 45, editor.delta_time);
			editor.cursor_pos.y = smooth_move(editor.cursor_pos.y, y, 45, editor.delta_time);

			if (editor.mode != Mode_Insert) {
				draw_capsule(editor.cursor_pos, {space_width, gfx.line_height}, Color::cursor);

				if (cursor_offset < line.len) {
					s32 width = 0;
					utf8_decode(slice(line, cursor_offset, line.len), &width);
					string cursor_char = slice(line, cursor_offset, cursor_offset + width);

					draw_text(cursor_char, editor.cursor_pos, Color::bg);
				}
			} else {
				draw_quad(editor.cursor_pos, {2, gfx.line_height}, Color::cursor);
			}
		}

		y += gfx.line_height;
		if (y > region.from.y + region.size.y) break;
	}
}

int main()
{
	editor.persist = arena_new(GB(1));
	editor.transient = arena_new(MB(32));
    
	graphics_init("text editor", 1280, 800, editor.persist);
    
	{
		auto buf = alloc_slice(editor.persist, Buffer, 16);
		editor.buffers = list_from_buffer(buf);
		append(&editor.buffers, {});
	}
    
	auto buf = &editor.buffers[0];
    
	buffer_make(
		buf,
		alloc_slice(editor.persist, u8, KB(512)),
		alloc_slice(editor.persist, Line, 2048)
	);

	Frame_Input input = {};
	u64 last_frame_time = platform_time_now();
	while(graphics_update(Hex(0x000000FF), &input))
	{
		defer(arena_free(editor.transient));

		u64 curr_time = platform_time_now();
		editor.delta_time = (f32) platform_time_diff(last_frame_time, curr_time).seconds;
		editor.time += editor.delta_time;
		last_frame_time = curr_time;

		rune c = input.character;

		graphics_push_clip({ {0, 0}, {(f32) gfx.win->w , (f32) gfx.win->h }}, editor.transient);
		defer(graphics_pop_clip());
        
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
				if      (input.key_flags & key_Escape) editor.mode = Mode_Normal;
				else if (input.key_flags & key_Delete) buffer_delete(buf, 1, Direction_Right);
				else if (input.key_flags & key_Backspace) buffer_delete(buf, 1, Direction_Left);
				else if (c) {
					string input = utf8_encode(c, editor.transient);
					buffer_insert(buf, input);
				}
			} break;
            
			case Mode_Command:
				command_palette_interface(input);
				break;

			default:
				break;
		}    

		draw_buffer_to_rect(buf, {{0, 0}, {(f32) gfx.win->w, (f32)gfx.win->h}});

		draw_quad({0, gfx.win->h - gfx.line_height}, {(f32) gfx.win->w, gfx.line_height}, Color::bg_alt);

		string mode_string = string_format(editor.transient, "-- %.*s --", s_fmt(MODE_STRING[editor.mode]));
		draw_text(mode_string, {0, gfx.win->h - gfx.line_height}, Color::accent);
    }
}
