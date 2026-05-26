#include "editor.h"
#include "config.h"

#include <stdio.h>

funcdef void
buffer__build_lines(Buffer *buffer)
{
	clear(&buffer->lines);
	bytes data = slice_from_list(buffer->data);
	
	for(u64 i=0; i<data.len; ++i)
	{
		if (data[i] == '\n') {
			Line entry = { i };

			append(&buffer->lines, entry);
		}
	}

	Line eof_entry = { data.len };
	append(&buffer->lines, eof_entry);
}

funcdef void
buffer__sync_desired_column(Buffer *buf)
{
	u64       line     = buffer_line_at_index(buf, buf->cursor);
	Range_U64 range    = buffer_line_range(buf, line);
	bytes     data     = slice_from_list(buf->data);
	string    line_str = string_from_bytes(slice(data, range.begin, buf->cursor));
	buf->desired_column = string_column_count(line_str, TAB_WIDTH);
}

funcdef u64
buffer__index_from_column(string s, u64 target_column)
{
	u64 column = 0;

	int width = 0;
	for (u64 i = 0; i < s.len; i += width) {
		rune c = utf8_decode(slice(s, i, s.len), &width);

		if (c == '\n') {
			break;
		}

		if (column >= target_column) {
			return i;
		}

		if (c == '\t') {
			column += TAB_WIDTH - (column % TAB_WIDTH);
		}
		else {
			column += 1;
		}
	}

	return s.len;
}


funcdef void
buffer_make(Buffer *buffer, u64 data_cap, u64 line_count, string path)
{
	MemZeroStruct(buffer);
	buffer->arena = arena_new(GB(1));
	buffer->path = string_copy(path, buffer->arena);
	buffer->data = list_from_buffer(alloc_slice(buffer->arena, u8, data_cap));
	buffer->lines = list_from_buffer(alloc_slice(buffer->arena, Line, line_count));
	buffer__build_lines(buffer);
}

funcdef void
buffer_deinit(Buffer *buffer)
{
	arena_delete(buffer->arena);
	MemZeroStruct(buffer);
}

funcdef void
buffer_insert(Buffer *buffer, string s, Arena *scratch)
{
	if (!buffer) return;

	bool move_left = false;
	if (s.len == 1) { // special case single char inputs
		char c = s[0];
		Char_Kind kind = char_kind(c);

		if (c == '\n') {
			u64 line_index = buffer_line_at_index(buffer, buffer->cursor);
			Range_U64 line_range = buffer_line_range(buffer, line_index);

			string data = string_from_bytes(slice_from_list(buffer->data));
			string current_line = slice(data, line_range.begin, line_range.end);

			u64 i=0;
			while(i < current_line.len && is_space(current_line[i]))
				i += 1;

			string indents = slice(current_line, 0, i);

			s = string_concat(s, indents, scratch);
		}
		else if (kind == Char_Open) {
			u8 close = (char) char_get_pair(c);
			string close_str = { &close, 1 };
			s = string_concat(s, close_str, scratch);
			move_left = true;
		}
		else if (kind == Char_Quote) {
			u8 close = (char) char_get_pair(c);
			string close_str = { &close, 1 };

			if (buffer->cursor < buffer->data.len) {
				string data = string_from_bytes(
					slice_from_list(buffer->data)
				);
				int width = 0;
				rune r = utf8_decode(slice(data, buffer->cursor, data.len), &width);

				if (r == c) {
					buffer_move_cursor(buffer, 1, Direction_Right);
					return;
				} else {
					s = string_concat(s, close_str, scratch);
					move_left = true;
				}
			} else {
				s = string_concat(s, close_str, scratch);
				move_left = true;
			}
		}
		else if(kind == Char_Close) {
			if (buffer->cursor < buffer->data.len) {

				string data = string_from_bytes(
					slice_from_list(buffer->data)
				);
				int width = 0;
				rune r = utf8_decode(slice(data, buffer->cursor, data.len), &width);

				if (r == c) {
					buffer_move_cursor(buffer, 1, Direction_Right);
					return;
				}
			}
		}
	}

	u64 needed_data_len  = buffer->data.len + s.len;
	if (needed_data_len > buffer->data.capacity) {
		u64 required_size = Max(buffer->data.capacity * 2, needed_data_len * 2);

		bytes old_data = {
			buffer->data.raw,
			buffer->data.capacity,
		};

		bytes new_data = realloc_slice(buffer->arena, u8, old_data, required_size);
		buffer->data.raw = new_data.raw;
		buffer->data.capacity = new_data.len;
	}

	u64 needed_lines_len = string_count_lines(s) + buffer->lines.len;
	if (needed_lines_len > buffer->lines.capacity) {
		u64 required_size = Max(buffer->lines.capacity * 2, needed_lines_len * 2);

		bytes old_data = {
			buffer->data.raw,
			buffer->data.capacity
		};

		bytes new_data = realloc_slice(buffer->arena, u8, old_data, required_size);
		buffer->data.raw = new_data.raw;
		buffer->data.capacity = new_data.len;
	}

	bytes insert_data = { (u8 *) s.raw, s.len };
	insert_slice(&buffer->data, buffer->cursor, insert_data);
	buffer->cursor += s.len;

	if (move_left) {
		buffer_move_cursor(buffer, 1, Direction_Left);
	}

	buffer__build_lines(buffer);
	buffer__sync_desired_column(buffer);
}

funcdef void
buffer_move_cursor(Buffer *buf, u64 amount, Direction dir)
{
	if (!buf) return;

	switch (dir) {

	case Direction_Left:
	case Direction_Right: {
		for (u64 step = 0; step < amount; ++step)
		{
			s64 delta = (dir == Direction_Left) ? -1 : +1;

			if (dir == Direction_Left) {
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

		// horizontal movement resets desired column to actual column
		{
			u64 line        = buffer_line_at_index(buf, buf->cursor);
			Range_U64 range = buffer_line_range(buf, line);
			bytes data      = slice_from_list(buf->data);
			string line_str = string_from_bytes(slice(data, range.begin, buf->cursor));
			buf->desired_column = string_column_count(line_str, TAB_WIDTH);
		}

	} break;

	case Direction_Up:
	case Direction_Down: {
		for (u64 step = 0; step < amount; ++step)
		{
			u64 line = buffer_line_at_index(buf, buf->cursor);

			if (dir == Direction_Up) {
				if (line == 0) break;
				line -= 1;
			}
			else {
				// last entry in lines[] is the EOF sentinel
				if (line + 1 >= buf->lines.len) break;
				line += 1;
			}

			Range_U64 range  = buffer_line_range(buf, line);
			bytes data        = slice_from_list(buf->data);
			string line_str   = string_from_bytes(slice(data, range.begin, range.end));

			u64 byte_offset   = buffer__index_from_column(line_str, buf->desired_column);
			buf->cursor       = range.begin + byte_offset;
		}

		// vertical movement does NOT update desired_column

	} break;

	default:
		break;
	}
}


funcdef void
buffer_move_cursor_to(Buffer *buffer, u64 index)
{
	if (!buffer || index > buffer->data.len) return;

	buffer->cursor = index;
	buffer__sync_desired_column(buffer);
}

funcdef void
buffer_delete(Buffer *buffer, u64 count, Direction direction)
{
	if (!buffer) return;

	auto buf = &buffer->data;

	if (count == 0 || buf->len == 0)
		return;

	u64 start = buffer->cursor;
	u64 end   = buffer->cursor;

	if (direction == Direction_Right)
	{
		if (start >= buf->len)
			return;

		while (count && end < buf->len)
		{
			end += 1;

			while (end < buf->len &&
			       utf8_continuation_byte((*buf)[end]))
			{
				end += 1;
			}

			count -= 1;
		}
	}
	else
	{
		if (start == 0)
			return;

		end = start;

		while (count && start > 0)
		{
			start -= 1;

			while (start > 0 &&
			       utf8_continuation_byte((*buf)[start]))
			{
				start -= 1;
			}

			count -= 1;
		}

		buffer->cursor = start;
	}

	if (end <= start)
		return;

	memmove(buf->raw + start,
	        buf->raw + end,
	        buf->len - end);

	buf->len -= (end - start);
	buf->raw[buf->len] = 0;

	buffer__build_lines(buffer);
	buffer__sync_desired_column(buffer);
}

funcdef Slice<string>
buffer_as_lines(Buffer *buffer, Arena *allocator)
{
	if (!buffer) return {};

	Slice<string> lines = alloc_slice(allocator, string, buffer->lines.len);
	bytes data = slice_from_list(buffer->data);

	for (u64 i=0; i<buffer->lines.len; ++i)
	{
		u64 begin = 0;
		if (i != 0) {
			begin = buffer->lines[i - 1].index + 1;
		}
		u64 end = buffer->lines[i].index;

		string line_string = string_from_bytes(slice(data, begin, end));
		lines[i] = line_string;
	}

	return lines;
}

funcdef Range_U64
buffer_line_range(Buffer *buffer, u64 line_index)
{
	if (!buffer) return {};

	if (line_index >= buffer->lines.len) {
		return Range_U64 {
			buffer->lines[line_index - 1].index + 1,
			buffer->data.len
		};
	}

	u64 end = buffer->lines[line_index].index;
	u64 begin = 0;
	if (line_index != 0) begin = buffer->lines[line_index - 1].index + 1;
	return Range_U64 { begin, end };
}


funcdef u64 
buffer_line_at_index(Buffer *buffer, u64 array_index)
{
	if (!buffer) return 0;

	Slice<Line> lines = slice_from_list(buffer->lines);
	u64 lower = 0;
	u64 higher = lines.len;

	while (lower < higher) {
		u64 mid = lower + (higher - lower) / 2;

		if (lines[mid].index < array_index)
			lower = mid + 1;
		else
			higher = mid;
	}

	return lower;
}
