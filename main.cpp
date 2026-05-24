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
	Arena *persist_arena;
	Arena *frame_arena;

	Arena *buffer_arena;

	Editor_Mode mode;

	/////////////////////////////
	// ~geb: memory

	List<Buffer> buffers;

	Free_Node *free_buffers;
	Free_Node *free_lines;

	/////////////////////////////
	// ~geb: ui

	f32 delta_time;
} editor;

funcdef void
insert_and_handle_overflow(Buffer *buffer, string s)
{
	Overflow overflow = {};
	buffer_insert(buffer, s, &overflow);

	bool any_overflow = (bool) (overflow.data_size || overflow.line_count);

	if (overflow.data_size) {
		Slice<u8> old_data = { buffer->data.raw, buffer->data.capacity };

		Slice<u8> new_data = realloc_slice(editor.buffer_arena, u8, old_data, overflow.data_size);
		buffer->data.raw = new_data.raw;
		buffer->data.capacity = new_data.len;

		if (old_data.raw != new_data.raw) {
			assert(old_data.len >= sizeof(Free_Node));

			Free_Node *free_node = (Free_Node *) old_data.raw;
			free_node->data = old_data;
			free_node->next = editor.free_buffers;
			editor.free_buffers = free_node;
		}

		printf("overflow handled!\n");
	}
	if (overflow.line_count) {
		Slice<Line> old_table = {
			buffer->lines.raw,
			buffer->lines.capacity
		};

		Slice<Line> new_table = realloc_slice(editor.buffer_arena, Line, old_table, overflow.line_count);
		buffer->lines.raw = new_table.raw;
		buffer->lines.capacity = new_table.len;

		if (old_table.raw != new_table.raw) {
			u8 *old_data_raw = (u8 *) old_table.raw;
			u64 old_data_len = old_table.len * sizeof(Line);

			bytes old_data = { old_data_raw, old_data_len };
			assert(old_data_len > sizeof(Free_Node));

			Free_Node *free_node = (Free_Node *) old_data_raw;
			free_node->data = old_data;
			free_node->next = editor.free_buffers;
			editor.free_buffers = free_node;
		}
	}

	if (any_overflow) buffer_insert(buffer, s);
}

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
		return {};
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

		graphics_push_clip(clip, editor.frame_arena);
		defer(graphics_pop_clip());

		string str = {
			.raw = cmd_string.raw,
			.len = cmd_string.len
		};

		vec2 text_pos = {x0, y0};
		vec2 size = draw_text(str, text_pos, Color::cursor);
		text_pos.x += size.x;

		draw_quad(text_pos,  {2, gfx.line_height}, Color::cursor);
	}

	return {};
}

funcdef void
draw_buffer_view(Buffer *buffer, Rect region)
{
	draw_quad(region.from, region.size, Color::bg);

	graphics_push_clip(region, editor.frame_arena);
	defer(graphics_pop_clip());

	local_persist f32 space_width = graphics_char_width(' ');

	f32 y = region.from.y;

	f32 region_width = region.size.x;

	Slice<string> lines = buffer_as_lines(buffer, editor.frame_arena);
	
	u64 line_at_cursor = buffer_line_at_index(buffer, buffer->cursor);
	Range_U64 cursor_line_range = buffer_line_range(buffer, line_at_cursor);

	for (u64 i=0; i<lines.len; ++i) {
		string line = lines[i];

		bool on_cursor = (i == line_at_cursor);

		if (on_cursor) { // draw cursor line
			draw_quad({region.from.x, y}, { region_width, gfx.line_height}, Color::bg_alt);
			draw_quad({region.from.x, y + 2}, { region_width, gfx.line_height - 4}, Color::bg);
		}

		string number_string = string_format(editor.frame_arena, "%zu", i + 1);
		draw_text(number_string, { region.from.x, y }, on_cursor ? Color::accent : Color::dim);

		f32 x = region.from.x + graphics_char_width('0') * (1 + Max(digit_count_u64(lines.len), 2));

		draw_text(line, {x, y}, Color::fg);

		if (on_cursor && editor.mode != Mode_Command) {
			u64 cursor_offset = buffer->cursor - cursor_line_range.begin;
			string before_cursor = slice(line, 0, cursor_offset);
			f32 cursor_x = x + graphics_measure_text(before_cursor).x;

			vec2 cursor_pos = { cursor_x, y };

			if (editor.mode != Mode_Insert) {
				draw_capsule(cursor_pos, {space_width, gfx.line_height}, Color::cursor);

				if (cursor_offset < line.len) {
					s32 width = 0;
					utf8_decode(slice(line, cursor_offset, line.len), &width);
					string cursor_char = slice(line, cursor_offset, cursor_offset + width);

					draw_text(cursor_char, cursor_pos, Color::bg);
				}
			} else {
				draw_quad(cursor_pos, {2, gfx.line_height}, Color::cursor);
			}
		}

		y += gfx.line_height;
		if (y > region.from.y + region.size.y) break;
	}
}

funcdef bool
editor_update()
{
	Frame_Input input = {};
	bool window_close = graphics_update(&input);

	graphics_push_clip({ {0, 0}, {(f32) gfx.win->w , (f32) gfx.win->h }}, editor.frame_arena);
	defer(graphics_pop_clip());

	auto buf = &editor.buffers[0];

	rune c = input.character;

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
				string input = utf8_encode(c, editor.frame_arena);
				insert_and_handle_overflow(buf, input);
			}
		} break;

		default:
			break;
	}

	draw_buffer_view(buf, {{0, 0}, {(f32) gfx.win->w, (f32)gfx.win->h}});

	if (editor.mode == Mode_Command) {
		command_palette_interface(input);
	}

	draw_quad({0, gfx.win->h - gfx.line_height}, {(f32) gfx.win->w, gfx.line_height}, Color::bg_alt);

	string mode_string = string_format(editor.frame_arena, "-- %.*s --", s_fmt(MODE_STRING[editor.mode]));
	draw_text(mode_string, {0, gfx.win->h - gfx.line_height}, Color::accent);

	graphics_submit_draw();
	return window_close;
}

int main()
{
	editor.persist_arena = arena_new(MB(32));
	editor.frame_arena = arena_new(MB(32));
	editor.buffer_arena = arena_new(GB(1));

	graphics_init("text editor", 1280, 800, editor.persist_arena);

	{
		auto buf = alloc_slice(editor.persist_arena, Buffer, 16);
		editor.buffers = list_from_buffer(buf);
		append(&editor.buffers, {});
	}

	auto buf = &editor.buffers[0];

	buffer_make(
		buf,
		alloc_slice(editor.buffer_arena, u8, KB(512)),
		alloc_slice(editor.buffer_arena, Line, 2048)
	);

	string input = string_from_bytes(platform_load_entire_file(S("main.cpp"), editor.frame_arena));
	insert_and_handle_overflow(buf, input);

	buf->cursor = 0;

	u64 last_frame_time = platform_time_now();
	for (bool quit = false; !quit;)
	{
		{
			// ~geb: prepare state for next frame
			u64 curr_time = platform_time_now();
			editor.delta_time = (f32) platform_time_diff(last_frame_time, curr_time).seconds;
			last_frame_time = curr_time;
		}

		quit = editor_update();

		arena_free(editor.frame_arena);
	}

}
