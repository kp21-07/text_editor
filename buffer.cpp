#include "editor.h"
#include "config.h"

#include <math.h>

funcdef void
buffer__build_lines(Buffer *buffer, u64 from)
{
    u64 line = buffer_line_index_at(buffer, from);

    u64 scan_start = 0;
    if (line > 0)
        scan_start = buffer->lines[line - 1].index + 1;

    buffer->lines.len = line;

    string data = buffer->data.view();

    for (u64 i = scan_start; i < data.len; ++i)
    {
        if (data[i] == '\n')
            append(&buffer->lines, {i});
    }

    append(&buffer->lines, {data.len});
}

funcdef void
buffer__sync_desired_column(Buffer *buffer)
{
	u64       line      = buffer_line_index_at(buffer, buffer->cursor);
	Range_u64 range     = buffer_line_range(buffer, line);
	string    data      = buffer->data.view();
	string    line_str  = data.range(range.begin, buffer->cursor);
	buffer->desired_col = string_column_count(line_str, TAB_WIDTH);
}

funcdef u64
buffer__index_from_column(string s, u64 target_column)
{
	u64 column = 0;
	int width = 0;

	for (u64 i = 0; i < s.len; i += width) {
		rune c = utf8_decode(s.range(i, s.len), &width);

		if (c == '\n') 
			break;

		if (column >= target_column) 
			return i;

		if (c == '\t') 
			column += TAB_WIDTH - (column % TAB_WIDTH);
		else 
			column += 1;
	}

	return s.len;
}

funcdef Load_Error
buffer_init(Buffer *buffer, string path)
{
	Temp t = temp_begin(scratch());
	defer(temp_end(t));

	MemZeroStruct(buffer);

	buffer->arena = arena_make(GB(1));
	buffer->path = path;

	Load_Error err = Load_Ok;
	{
		OS_FileData file_data = os_file_data(path);
		if (!Flag_Check(file_data.flags, File_Exists)) {

			buffer->data = list_make(alloc_slice(buffer->arena, u8, KB(512)));
			buffer->lines = list_make(alloc_slice(buffer->arena, Line, 2048));
			buffer__build_lines(buffer, 0);
			return Load_Invalid_Path;
		}
		buffer->file_kind = file_data.kind;

		bytes data = alloc_slice(buffer->arena, u8, Max(file_data.size * 2, KB(512)));
		u64 len = 0;
		Load_Error err = os_file_to_buffer(data.raw, data.len, &len, path);

		if (err != Load_Ok) {
			buffer->data = list_make(alloc_slice(buffer->arena, u8, KB(512)));
			buffer->lines = list_make(alloc_slice(buffer->arena, Line, 2048));
			buffer__build_lines(buffer, 0);
			return err;
		}

		buffer->data = list<u8> {
			data.raw,
			len,
			data.len
		};
	}

	string buf_string = string_from_list(buffer->data);

	u64 line_count = Max((string_count_lines(buf_string) * 2), 2048);
	buffer->lines = list_make(alloc_slice(buffer->arena, Line, line_count));
	buffer__build_lines(buffer, 0);

	Flag_Set(buffer->flags, Buffer_Occupied);
	return err;
}


funcdef void
buffer_deinit(Buffer *buffer)
{
	arena_delete(buffer->arena);
	MemZeroStruct(buffer);
}


funcdef u64 
buffer_line_count(Buffer *buffer)
{
	if (!buffer)
		return 0;

	return buffer->lines.len;
}

funcdef u64
buffer_line_index_at(Buffer *buffer, u64 buf_index)
{
	if (!buffer)
		return 0;

	auto lines = buffer->lines.view();
	u64 lower = 0;
	u64 higher = lines.len;

	while (lower < higher) {
		u64 mid = lower + (higher - lower) / 2;

		if (lines[mid].index < buf_index)
			lower = mid + 1;
		else
			higher = mid;
	}

	return lower;
}


funcdef Range_u64
buffer_line_range(Buffer *buffer, u64 line_index)
{
	if (!buffer) return {};

	if (line_index >= buffer->lines.len) {
		u64 begin = buffer->lines.len > 0
			? buffer->lines[buffer->lines.len - 1].index + 1
			: 0;
		return Range_u64 { begin, buffer->data.len };
	}

	u64 end = buffer->lines[line_index].index;
	u64 begin = 0;
	if (line_index != 0)
		begin = buffer->lines[line_index - 1].index + 1;
	return Range_u64 { begin, end };
}


funcdef void
buffer_insert(Buffer *buffer, string s)
{
	if (!buffer) return;
	Flag_Set(buffer->flags, Buffer_Dirty);

	Temp t = temp_begin(scratch());
	defer(temp_end(t));

	bool move_left = false;

	u64 needed_data_len  = buffer->data.len + s.len;
	if (needed_data_len > buffer->data.capacity) {
		u64 required_size = Max(buffer->data.capacity * 2, needed_data_len * 2);
		list_realloc(&buffer->data, required_size, buffer->arena);
	}

	u64 needed_lines_len = string_count_lines(s) + buffer->lines.len;
	if (needed_lines_len > buffer->lines.capacity) {
		u64 required_size = Max(buffer->lines.capacity * 2, needed_lines_len * 2);
		list_realloc(&buffer->lines, required_size, buffer->arena);
	}

	u64 before_insert = buffer_cursor(buffer);
	bytes insert_data = { (u8 *) s.raw, s.len };
	insert_slice(&buffer->data, buffer->cursor, insert_data);
	buffer->cursor += s.len;

	buffer__build_lines(buffer, before_insert);
	buffer__sync_desired_column(buffer);
}


funcdef void
buffer_delete(Buffer *buffer, u64 count, Direction direction)
{
    if (!buffer) return;

    auto buf = buffer->data.view();

    if (count == 0 || buf.len == 0)
        return;

    u64 start = buffer->cursor;
    u64 end   = buffer->cursor;

    if (direction == Direction::Right)
    {
        while (count-- && end < buf.len)
            end = utf8_next_boundary(buf, end);
    }
    else
    {
        while (count-- && start > 0)
            start = utf8_prev_boundary(buf, start);

        end = buffer->cursor;
        buffer->cursor = start;
    }

    if (start == end)
        return;

	u8 *mem = (u8 *) buf.raw;
    memmove(mem + start, mem + end, buf.len - end);
	buffer->data.len -= (end - start);

    buffer__build_lines(buffer, start);
    buffer__sync_desired_column(buffer);
	Flag_Set(buffer->flags, Buffer_Dirty);
}

funcdef void
buffer_move_cursor(Buffer *buf, u64 amount, Direction dir)
{
	if (!buf)
		return;

	switch (dir) {

	case Direction::Absolute:
	{
		buf->cursor = amount;
		buffer__sync_desired_column(buf);
	} break;

	case Direction::Left:
	case Direction::Right: {
		for (u64 step = 0; step < amount; ++step)
		{
			s64 delta = (dir == Direction::Left) ? -1 : +1;

			if (dir == Direction::Left) {
				if (buf->cursor == 0) break;
			}
			else {
				if (buf->cursor >= buf->data.len) break;
			}

			buf->cursor += delta;

			while (
				buf->cursor > 0 &&
				buf->cursor < buf->data.len &&
				utf8_continuation_byte(buf->data[buf->cursor])
			) {
				buf->cursor += delta;
			}
		}

		buffer__sync_desired_column(buf);
	} break;

	case Direction::Up:
	case Direction::Down: {
		for (u64 step = 0; step < amount; ++step)
		{
			u64 line = buffer_line_index_at(buf, buf->cursor);

			if (dir == Direction::Up) {
				if (line == 0) break;
				line -= 1;
			}
			else {
				if (line + 1 >= buf->lines.len)
					break;
				line += 1;
			}

			Range_u64 range  = buffer_line_range(buf, line);
			string data       = buf->data.view();
			string line_str   = data.range(range.begin, range.end);
			u64 byte_offset   = buffer__index_from_column(line_str, buf->desired_col);
			buf->cursor       = range.begin + byte_offset;
		}
	} break;

	default:
		break;
	}
}

funcdef rune
buffer_char_at(Buffer *buf, s64 index)
{
	if (!buf)
		return 0;

	if (index >= buf->data.len || index < 0)
		return 0;

	int width = 0;
	return utf8_decode(buf->data.view().range(index, buf->data.len), &width);
}


funcdef u64 
buffer_cursor(Buffer *buf)
{
	if (!buf)
		return 0;

	return buf->cursor;
}

funcdef string
buffer_slice(Buffer *buffer, Arena *arena, Range_u64 range) {
	if (!buffer)
		return S("");

	string view = buffer->data.view().range(range.begin, range.end);
	return string_copy(arena, view);
}

///////////////////////////////////////

funcdef u64
hash_path(string path)
{
    uint64_t h = 14695981039346656037ULL; 

    const u8 *p = (const u8 *)path.raw;

    for (size_t i = 0; i < path.len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }

    return h;
}

funcdef Buffer_Map
buffer_map_make(Arena *arena, u64 capacity)
{
	Buffer_Map map = {};
	map.table = alloc_slice(arena, Buffer, capacity);
	return map;
}

funcdef void
buffer_map_clear(Buffer_Map *map)
{
	slice<Buffer> table = map->table;
	u64 capacity = table.len;

	for (u64 i=0; i<capacity; ++i) {

		if (!Flag_Check(table[i].flags, Buffer_Occupied))
			continue;

		buffer_deinit(&table[i]);
	}
	map->count = 0;

	if (map->table.raw) {
		memset(map->table.raw, 0x0, sizeof(Buffer) * map->table.len);
	}
}

funcdef Buffer *
buffer_map_insert(Buffer_Map *map, const Buffer& buffer)
{
	Temp t = temp_begin(scratch());
	defer(temp_end(t));

	string path = buffer.path;
	if (path.len == 0) {
		return nullptr;
	}

	path = os_path_canonical(t.arena, path);

	slice<Buffer> table = map->table;
	u64 capacity = table.len;
	u64 index = hash_path(path) % capacity;

	for(u64 i=0; i<capacity; ++i) {

		if (!Flag_Check(table[index].flags, Buffer_Occupied)) {
			table[index] = buffer;
			Flag_Set(table[index].flags, Buffer_Occupied);
			return &table[index];
		}

		index = (index + 1) % capacity;
	}

	return nullptr;
}


funcdef Buffer *
buffer_map_get(Buffer_Map *map, string path)
{
	Temp t = temp_begin(scratch());
	defer(temp_end(t));

	path = os_path_canonical(t.arena, path);
	slice<Buffer> table = map->table;
	u64 capacity = table.len;
	u64 index = hash_path(path) % capacity;

	for(u64 i=0; i<capacity; ++i) {
		Temp t2 = temp_begin(scratch());
		defer(temp_end(t2));

		if (!Flag_Check(table[index].flags, Buffer_Occupied))
		{
			return nullptr;
		}

		string path2 = os_path_canonical(t2.arena, table[index].path);
		if (string_equal(path2, path))
			return &table[index];

		index = (index + 1) % capacity;
	}
	
	return nullptr;
}


funcdef bool
buffer_map_remove(Buffer_Map *map, string path)
{
	Temp t = temp_begin(scratch());
	defer(temp_end(t));

	path = os_path_canonical(t.arena, path);
	slice<Buffer> table = map->table;
	u64 capacity = table.len;
	u64 index = hash_path(path) % capacity;

	for (u64 i=0; i<capacity; ++i) {
		Temp t2 = temp_begin(scratch());
		defer(temp_end(t2));

		if (!Flag_Check(table[index].flags, Buffer_Occupied))
			return false;


		string path2 = os_path_canonical(t2.arena, table[index].path);
		if (string_equal(path2, path)) {
			buffer_deinit(&table[index]); // clears flags too
		}

		index = (index + 1) % capacity;
	}

	return false;
}

funcdef slice<string>
buffer_map_get_paths(Buffer_Map *map, Arena *arena)
{
	list<string> result = list_make(alloc_slice(arena, string, map->count));

	for (u64 i=0; i<map->table.len; ++i) {
		if (!Flag_Check(map->table[i].flags, Buffer_Occupied))
			continue;

		append(&result, map->table[i].path);
	}

	return { result.raw, result.len };
}

/////////////////////////////////////


funcdef void
draw_buffer_view(Buffer *buffer, Quad rect)
{
	gfx_push_clip(rect);
	defer(gfx_pop_clip());

	if (!buffer) {
		vec2 dim = gfx_measure_text(S(" no file "));

		f32 x = rect.from.x + (rect.size.x - dim.x) * 0.5f;
		f32 y = rect.from.y + (rect.size.y - dim.y) * 0.5f;
		gfx_draw_text(S(" no file "), {x, y}, THEME.error);
		return;
	}

	f32 line_h = line_height();
	f32 digit_width = char_pixels('0');
	f32 space_width = char_pixels(' ');

	string buf_string = buffer->data.view();
	auto lines = buffer->lines.view();

	u64 cursor_line = buffer_line_index_at(buffer, buffer->cursor);
	Range_u64 cursor_range = buffer_line_range(buffer, cursor_line);

	// scrolling

	f32 cursor_y = cursor_line * line_h;
	f32 margin = 5 * line_h;

	if (cursor_y < buffer->target_scroll_y + margin) {
		buffer->target_scroll_y = cursor_y - margin;
	}

	if (cursor_y + line_h > buffer->target_scroll_y + rect.size.y - margin) {
		buffer->target_scroll_y = cursor_y + line_h + margin - rect.size.y;
	}

	f32 max_scroll = Max(lines.len * line_h - rect.size.y, 0);
	buffer->target_scroll_y = Clamp(buffer->target_scroll_y, 0, max_scroll);
	buffer->scroll_y = Lerp(
		buffer->scroll_y,
		buffer->target_scroll_y,
		1.0f - expf(-20.0f * delta_time())
	);
	// buffer->scroll_y = buffer->target_scroll_y;

	// gutter

	u64 gutter_digits = Max(digit_count_u64(lines.len), 2);

	f32 gutter_pad   = digit_width;
	f32 gutter_width = gutter_pad * 2 + digit_width * gutter_digits;

	f32 text_x = rect.from.x + gutter_width;

	// visible lines

	u64 first_visible_line = (u64)(buffer->scroll_y / line_h);
	f32 y = rect.from.y - fmodf(buffer->scroll_y, line_h);

	// draw lines

	for (u64 i = first_visible_line; i < lines.len; ++i) {
		Temp t0 = temp_begin(scratch());
		defer(temp_end(t0));

		if (y > rect.from.y + rect.size.y) {
			break;
		}

		Range_u64 range = buffer_line_range(buffer, i);
		string line = buf_string.range(range.begin, range.end);

		bool current_line = (i == cursor_line);

		if (current_line) {
			gfx_draw_quad(
				{text_x, y, rect.size.x - gutter_width - 2, line_h},
				{},
				THEME.background_dim,
				THEME.radius
			);
		}

		string line_number = string_format(
			scratch(),
			"%*zu",
			(int)gutter_digits,
			i + 1
		);

		if (current_line) {
			gfx_draw_quad(
				{rect.from.x + gutter_pad, y, digit_width * gutter_digits, line_h},
				{},
				THEME.gutter_foreground,
				5
			);
		}

		gfx_draw_text(
			line_number,
			{rect.from.x + gutter_pad, y},
			current_line ? THEME.background : THEME.gutter_foreground
		);

		vec4 rect = gfx_draw_text(line, {text_x, y}, THEME.foreground);
		vec2 size = { rect.z, rect.w };

		if (current_line && ed_mode() != Ed_Mode::Command) {
			u64 cursor_offset = buffer->cursor - cursor_range.begin;

			string before_cursor = line.range(0, cursor_offset);

			f32 cursor_x = text_x + gfx_measure_text(before_cursor).x;

			vec2 cursor_pos = {cursor_x, y};

			if (ed_mode() != Ed_Mode::Insert) {
				gfx_draw_quad({cursor_pos.x, cursor_pos.y, space_width, line_h}, {}, THEME.cursor, 9999.9f);

				if (cursor_offset < line.len) {
					s32 width = 0;

					utf8_decode(line.range(cursor_offset, line.len), &width);

					string cursor_char = line.range(cursor_offset, cursor_offset + width);

					gfx_draw_text(cursor_char, cursor_pos, THEME.cursor_text);
				}
			} else {
				gfx_draw_quad({cursor_pos.x, cursor_pos.y, 2, line_h}, {}, THEME.cursor, 9999.9f);
			}
		}

		y += Max(size.y, line_h);
	}
}
