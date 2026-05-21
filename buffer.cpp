#include "editor.h"

funcdef void
buffer__build_lines(Buffer *buffer)
{
	bytes data = slice_from_list(buffer->data);
	for (u64 i=0; i<data.len; ++i) {
	}
}

funcdef void
buffer_make(Buffer *buffer, bytes data, Slice<Line> line_table)
{
	buffer->data = list_from_buffer(data);
	buffer->lines = list_from_buffer(line_table);
	buffer->cursor = 0;
}


funcdef void
buffer_insert(Buffer *buffer, string s, Overflow *overflow)
{
	if (overflow) {
		overflow->data_size = 0;
		overflow->line_count = 0; // @TODO: later when building line table
	}

	bool data_overflow = false;
	bool line_table_overflow = false;

	if (buffer->data.len + s.len > buffer->data.capacity) {
		u64 min_required  = buffer->data.len + s.len;
		u64 required_size = Max(buffer->data.capacity * 2, min_required);

		data_overflow = true;
		
		if (overflow) {
			overflow->data_size = required_size;
		}
	}

	if (data_overflow || line_table_overflow) return;

	bytes insert_data = { (u8 *) s.raw, s.len };
	insert_slice(&buffer->data, buffer->cursor, insert_data);

	buffer->cursor += s.len;

	// buffer__build_line_ends(buffer);
}

funcdef void
buffer_move_cursor(Buffer *buf, u64 amount, Direction dir)
{
	for (u64 step = 0; step < amount; ++step) {

		switch (dir) {
			case Direction_Left:
			case Direction_Right: {
				int delta = 0;
				if (dir == Direction_Left) {
					if (buf->cursor == 0) break;
					delta = -1;
				}
				else
				{
					if (buf->cursor >= buf->data.len) break;
					delta = +1;
				}

				buf->cursor += delta;

				while (buf->cursor < buf->data.len && utf8_continuation_byte(buf->data[buf->cursor]))
				{
					buf->cursor += delta;
				}
			} break;

			case Direction_Up:
			case Direction_Down: {
				int delta = 0;
				if (dir == Direction_Up) {
					delta = -1;
				} else {
					delta = +1;
				}
			}break;

			default:
			break;
		}
	}
}

funcdef void
buffer_delete(Buffer *buffer, u64 count, Direction direction)
{
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
}

funcdef Slice<string>
buffer_as_lines(Buffer *buffer, Arena *allocator)
{
    string buf_string = {
        .raw = (const u8 *) (buffer->data.raw),
        .len = buffer->data.len
    };

    u64 line_count = 1;
    for (u64 i = 0; i < buf_string.len; ++i) {
        if (buf_string[i] == '\n') line_count += 1;
    }

    Slice<string> lines = alloc_slice(allocator, string, line_count);
    u64 out = 0;
    u64 i0  = 0;

    for (u64 i = 0; i < buf_string.len; ++i) {
        if (buf_string[i] != '\n') continue;
        lines[out++] = slice(buf_string, i0, i);
        i0 = i + 1;
    }

    lines[out++] = slice(buf_string, i0, buf_string.len);

    return lines;
}
