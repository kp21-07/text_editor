#include "editor.h"
#include "config.h"

#include <math.h>
#include <type_traits>

funcdef void
buffer__build_tokens(Buffer *buffer, u64 from)
{
    u64 line_index = buffer_line_index_at(buffer, from);
	Tokenize_Proc tokenizer = buffer->tokenizer;
	if (!tokenizer)
		return;

    u64 scan_start  = 0;
    Lexer_State state = Lex_State_None;

    if (line_index > 0) {
        Line& prev = buffer->line_tbl[line_index - 1];
        scan_start = prev.index + 1;
        state      = prev.lex_state;

        u64 token_keep = prev.token_begin + prev.tokens_len;
        big_array_pop(&buffer->tokens, token_keep);
    } else {
        big_array_pop(&buffer->tokens, 0);
    }

    string source = buffer->data.view();
    u64 num_lines = buffer->line_tbl.len;

    for (u64 i = line_index; i < num_lines; ++i) {
        Line& line = buffer->line_tbl[i];

        Range_u64 range;
        range.begin = scan_start;
        range.end   = line.index;

        line.token_begin = buffer->tokens.len;
        line.lex_state   = state;

		tokenizer(&state, source, &buffer->tokens, range.begin, range.end);

        line.tokens_len = (u32)(buffer->tokens.len - line.token_begin);

        scan_start = line.index + 1;
    }
}

funcdef void
buffer__build_lines(Buffer *buffer, u64 from)
{
    u64 line = buffer_line_index_at(buffer, from);

    u64 scan_start = 0;
    if (line > 0)
        scan_start = buffer->line_tbl[line - 1].index + 1;

    big_array_pop(&buffer->line_tbl, line);

    string data = buffer->data.view();

    for (u64 i = scan_start; i < data.len; ++i)
    {
        if (data[i] == '\n') {
            Line l = {};
            l.index = i;
            big_array_push(&buffer->line_tbl, l);
        }
    }

    Line sentinel = {};
    sentinel.index = data.len;
    big_array_push(&buffer->line_tbl, sentinel);
}

funcdef void
buffer__sync_desired_column(Buffer *buffer)
{
	u64       line      = buffer_line_index_at(buffer, buffer->cursor);
	Range_u64 range     = buffer_line_range(buffer, line);
	string    data      = buffer->data.view();
	string    line_str  = data.range(range.begin, buffer->cursor);
	buffer->desired_col = string_column_count(line_str, cfg_u32(tab_width));
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
			column += cfg_u32(tab_width) - (column % cfg_u32(tab_width));
		else 
			column += 1;
	}

	return s.len;
}

funcdef Load_Error
buffer_init(Buffer *buffer, string path)
{
	Temp t = temp_begin(scratch(0, 0));
	defer(temp_end(t));

	MemZeroStruct(buffer);

	buffer->arena = arena_make(GB(1));
	buffer->line_tbl = big_array_make<Line>(2000000);
	buffer->tokens   = big_array_make<Lang_Token>(2000000);
	buffer->path = path;

	// History
	
	buffer->history.undo_stack = list_make(alloc_slice(buffer->arena, Edit_Transaction, 128));
	buffer->history.redo_stack = list_make(alloc_slice(buffer->arena, Edit_Transaction, 128));
	
	Load_Error err = Load_Ok;
	{
		OS_FileData file_data = os_file_data(path);
		buffer->file_kind = file_data.kind;

		switch (file_data.kind) {
			case OS_FileKind::C:
			case OS_FileKind::Cpp:    buffer->tokenizer = tokenize_source_code_cpp; break;

			case OS_FileKind::Config: buffer->tokenizer = tokenize_source_code_config; break;
			default: buffer->tokenizer = nullptr; break;
		}

		if (!Flag_Check(file_data.flags, File_Exists)) {

			buffer->data = list_make(alloc_slice(buffer->arena, u8, KB(512)));
			// buffer->lines = list_make(alloc_slice(buffer->arena, Line, 2048));
			buffer__build_lines(buffer, 0);
			buffer__build_tokens(buffer, 0);
			return Load_Invalid_Path;
		}

		bytes data = alloc_slice(buffer->arena, u8, Max(file_data.size * 2, KB(512)));
		u64 len = 0;
		Load_Error err = os_file_to_buffer(data.raw, data.len, &len, path);

		if (err != Load_Ok) {
			buffer->data = list_make(alloc_slice(buffer->arena, u8, KB(512)));
			// buffer->lines = list_make(alloc_slice(buffer->arena, Line, 2048));
			buffer__build_lines(buffer, 0);
			buffer__build_tokens(buffer, 0);
			return err;
		}

		buffer->data = list<u8> {
			data.raw,
			len,
			data.len
		};
	}

	string buf_string = string_from_list(buffer->data);
	buffer__build_lines(buffer, 0);
	buffer__build_tokens(buffer, 0);

	Flag_Set(buffer->flags, Buffer_Occupied);
	return err;
}


funcdef void
buffer_deinit(Buffer *buffer)
{
	arena_delete(buffer->arena);

	big_array_delete(&buffer->line_tbl);
	big_array_delete(&buffer->tokens);

	MemZeroStruct(buffer);
}


funcdef u64 
buffer_line_count(Buffer *buffer)
{
	if (!buffer)
		return 0;

	return buffer->line_tbl.len;
}

funcdef u64
buffer_line_index_at(Buffer *buffer, u64 buf_index)
{
	if (!buffer)
		return 0;

	auto lines = buffer->line_tbl.view();
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

	if (line_index >= buffer->line_tbl.len) {
		u64 begin = buffer->line_tbl.len > 0
			? buffer->line_tbl[buffer->line_tbl.len - 1].index + 1
			: 0;
		return Range_u64 { begin, buffer->data.len };
	}

	u64 end = buffer->line_tbl[line_index].index;
	u64 begin = 0;
	if (line_index != 0)
		begin = buffer->line_tbl[line_index - 1].index + 1;
	return Range_u64 { begin, end };
}


funcdef void
buffer_begin_transaction(Buffer *buffer)
{
	if (!buffer)
		return;

	if (buffer->history.has_active_group)
		return;

	buffer->history.has_active_group = true;
	buffer->history.active_group.edits = list_make(alloc_slice(buffer->arena, Edit_Op, 512));
	buffer->history.active_group.cursor_before = { buffer->cursor, ed_get_yank_region() };
	buffer->history.active_group.cursor_after = {};
}

funcdef void
buffer_end_transaction(Buffer *buffer)
{
	if (!buffer)
		return;

	if (!buffer->history.has_active_group)
		return;

	buffer->history.active_group.cursor_after = { buffer->cursor, ed_get_yank_region() };

	if (buffer->history.active_group.edits.len > 0) {
		append(&buffer->history.undo_stack, buffer->history.active_group);
		clear(&buffer->history.redo_stack);
	}

	buffer->history.has_active_group = false;
	buffer->history.active_group = {};
}

funcdef void
buffer_record_op(Buffer *buffer, Edit_Kind kind, u64 offset, string text)
{
    if (!buffer->history.has_active_group) {
        buffer_begin_transaction(buffer);
    }
    
    Edit_Op op = {};
    op.kind = kind;
    op.offset = offset;
    op.text = string_copy(buffer->arena, text);
    
    append(&buffer->history.active_group.edits, op);
}

funcdef void
buffer_insert(Buffer *buffer, string s, bool is_raw = false)
{
	if (!buffer) return;
	
	if (!is_raw) {
		buffer_record_op(buffer, Edit_Insert, buffer->cursor, s);
	}
	
	Temp t = temp_begin(scratch(0, 0));
	defer(temp_end(t));

	bool move_left = false;

	u64 needed_data_len  = buffer->data.len + s.len;
	if (needed_data_len > buffer->data.capacity) {
		u64 required_size = Max(buffer->data.capacity * 2, needed_data_len * 2);
		list_realloc(&buffer->data, required_size, buffer->arena);
	}

	u64 before_insert = buffer_cursor(buffer);
	bytes insert_data = { (u8 *) s.raw, s.len };
	insert_slice(&buffer->data, buffer->cursor, insert_data);
	buffer->cursor += s.len;

	buffer__build_lines(buffer, before_insert);
	buffer__build_tokens(buffer, before_insert);
	buffer__sync_desired_column(buffer);
}


funcdef void
buffer_delete(Buffer *buffer, u64 count, Direction direction, bool is_raw = false)
{
	if (!buffer) return;

	auto buf = buffer->data.view();

	if (count == 0 || buf.len == 0)
		return;

	u64 start = buffer->cursor;
	u64 end   = buffer->cursor;

	if (is_raw) {
		if (direction == Direction::Right) {
			end = Min(buf.len, buffer->cursor + count);
		} else {
			start = (buffer->cursor >= count) ? buffer->cursor - count : 0;
			buffer->cursor = start;
		}
	} else {
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
	}

	if (start == end)
		return;

	if (!is_raw) {
		// Record for Undo
		string deleted_text = buffer_slice(buffer, buffer->arena, {start, end});
		buffer_record_op(buffer, Edit_Delete, start, deleted_text);
	} 
  
	u8 *mem = (u8 *) buf.raw;
	memmove(mem + start, mem + end, buf.len - end);
	buffer->data.len -= (end - start);

	buffer__build_lines(buffer, start);
	buffer__build_tokens(buffer, start);
	buffer__sync_desired_column(buffer);
	Flag_Set(buffer->flags, Buffer_Dirty);
}

funcdef void
buffer_undo(Buffer *buf)
{
	if (!buf)
		return;

	if (buf->history.undo_stack.len == 0)
		return;

	buffer_end_transaction(buf);

	Edit_Transaction group = buf->history.undo_stack[buf->history.undo_stack.len - 1];
	buf->history.undo_stack.len -= 1;

	// Perform Inverse operation in reverse order
	for (s64 i = (s64) group.edits.len - 1; i >= 0; i--) {
		Edit_Op op = group.edits[i];
		if (op.kind == Edit_Insert) {
			buf->cursor = op.offset;
			buffer_delete(buf, op.text.len, Direction::Right, true);
		}
		else {
			buf->cursor = op.offset;
			buffer_insert(buf, op.text, true);
		}
	}

	buf->cursor = group.cursor_before.cursor;
	ed_set_yank_region(group.cursor_before.yank_region);

	append(&buf->history.redo_stack, group);
}

funcdef void
buffer_redo(Buffer *buf)
{
	if (!buf)
		return;

	if (buf->history.redo_stack.len == 0)
		return;

	Edit_Transaction group = buf->history.redo_stack[buf->history.redo_stack.len - 1];
	buf->history.redo_stack.len -= 1;

	// Perform original operation in forward order
	for (u64 i = 0; i < group.edits.len; i++) {
		Edit_Op op = group.edits[i];
		if (op.kind == Edit_Insert) {
			buf->cursor = op.offset;
			buffer_insert(buf, op.text, true);
		}
		else {
			buf->cursor = op.offset;
			buffer_delete(buf, op.text.len, Direction::Right, true);
		}
	}

	buf->cursor = group.cursor_after.cursor;
	ed_set_yank_region(group.cursor_after.yank_region);

	append(&buf->history.undo_stack, group);
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
				if (line + 1 >= buf->line_tbl.len)
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
	Temp t = temp_begin(scratch(0, 0));
	defer(temp_end(t));

	string path = buffer.path;
	if (path.len == 0) {
		return nullptr;
	}

	path = os_path_canonical(t.arena, path);

	slice<Buffer> table = map->table;
	u64 capacity = table.len;
	u64 index = hash_string(path) % capacity;

	for(u64 i=0; i<capacity; ++i) {

		if (!Flag_Check(table[index].flags, Buffer_Occupied)) {
			table[index] = buffer;
			Flag_Set(table[index].flags, Buffer_Occupied);
			map->count += 1;
			return &table[index];
		}

		index = (index + 1) % capacity;
	}

	return nullptr;
}


funcdef Buffer *
buffer_map_get(Buffer_Map *map, string path)
{
	Temp t = temp_begin(scratch(0, 0));
	defer(temp_end(t));

	path = os_path_canonical(t.arena, path);
	slice<Buffer> table = map->table;
	u64 capacity = table.len;
	u64 index = hash_string(path) % capacity;

	for(u64 i=0; i<capacity; ++i) {
		Temp t2 = temp_begin(scratch(&t.arena, 1));
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
    Temp t = temp_begin(scratch(0, 0));
    defer(temp_end(t));

    path = os_path_canonical(t.arena, path);
    slice<Buffer> table = map->table;
    u64 capacity = table.len;
    u64 index = hash_string(path) % capacity;

    for (u64 i = 0; i < capacity; ++i) {
        if (!Flag_Check(table[index].flags, Buffer_Occupied))
            return false;

        Temp t2 = temp_begin(scratch(&t.arena, 1));
        string path2 = os_path_canonical(t2.arena, table[index].path);
        temp_end(t2);

        if (string_equal(path2, path))
            break;

        index = (index + 1) % capacity;
    }

    buffer_deinit(&table[index]);
    map->count -= 1;

    u64 empty = index;
    u64 scan  = (index + 1) % capacity;

    while (Flag_Check(table[scan].flags, Buffer_Occupied)) {
        Temp t2 = temp_begin(scratch(&t.arena, 1));
        string scan_path = os_path_canonical(t2.arena, table[scan].path);
        u64 natural = hash_string(scan_path) % capacity;
        temp_end(t2);

        bool needs_shift =
            (empty <= scan)
                ? (natural <= empty || natural > scan)
                : (natural <= empty && natural > scan);

        if (needs_shift) {
            table[empty] = table[scan];
            buffer_deinit(&table[scan]);
            Flag_Set(table[empty].flags, Buffer_Occupied);
            MemZeroStruct(&table[scan]);
            empty = scan;
        }
        scan = (scan + 1) % capacity;
    }

    return true;
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


funcdef vec4
draw_line(string source, string line, u64 line_start, slice<const Lang_Token> tokens, vec2 pos)
{
	vec4 bounds = {};
	f32  x      = pos.x;

	u64 i = 0;

	for (u64 t = 0; t < tokens.len; t++) {
		Lang_Token tok = tokens[t];

		u64 tok_start = tok.source_offset - line_start;
		u64 tok_end   = tok_start + tok.len;

		// draw any unstyled gap before this token
		if (tok_start > i) {
			string gap = line.range(i, tok_start);
			vec4 b = gfx_draw_text(gap, {x, pos.y}, cfg_color(foreground));
			x      += b.z;
			bounds  = b;
		}

		vec4 color;
		switch (tok.kind) {
			case Token_Keyword:    color = cfg_color(keyword);    break;
			case Token_String:     color = cfg_color(string);     break;
			case Token_Number:     color = cfg_color(number);     break;
			case Token_Comment:    color = cfg_color(comment);    break;
			case Token_Macro:      color = cfg_color(macro);      break;
			case Token_Identifier: color = cfg_color(foreground); break;
			case Token_Symbol:     color = cfg_color(operator);   break;
			case Token_Type:       color = cfg_color(type);       break;
			default:               color = cfg_color(foreground); break;
		}

		u64 clipped_start = tok_start;
		u64 clipped_end   = Min(tok_end, line.len);

		if (clipped_start < clipped_end) {
			string text = line.range(clipped_start, clipped_end);
			vec4 b = gfx_draw_text(text, {x, pos.y}, color);
			x      += b.z;
			bounds  = b;
		}

		i = Min(tok_end, line.len);

		if (i >= line.len)
			break;
	}

	if (i < line.len) {
		string tail = line.range(i, line.len);
		vec4 b = gfx_draw_text(tail, {x, pos.y}, cfg_color(foreground));
		bounds = b;
	}

	return bounds;
}

funcdef void
draw_buffer_view(Buffer *buffer, Quad rect, bool is_active)
{
	gfx_push_clip(rect);
	defer(gfx_pop_clip());

	Range_u64 sel = ed_get_selection_region();

	if (!buffer) {
		vec2 dim = gfx_measure_text(S(" no file "));

		f32 x = rect.from.x + (rect.size.x - dim.x) * 0.5f;
		f32 y = rect.from.y + (rect.size.y - dim.y) * 0.5f;
		gfx_draw_text(S(" no file "), {x, y}, cfg_color(error));
		return;
	}

	f32 line_h = line_height();
	f32 digit_width = char_pixels('0');
	f32 space_width = char_pixels(' ');

	string buf_string = buffer->data.view();
	auto lines = buffer->line_tbl.view();

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
	buffer->scroll_y = buffer->target_scroll_y;

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
		Temp t0 = temp_begin(scratch(0, 0));
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
				cfg_color(current_line),
				cfg_f32(radius)
			);
		}

		string line_number = string_format(
			t0.arena,
			"%*zu",
			(int)gutter_digits,
			i + 1
		);

		if (current_line) {
			gfx_draw_quad(
				{rect.from.x + gutter_pad, y, digit_width * gutter_digits, line_h},
				{},
				cfg_color(line_highlight),
				cfg_f32(radius)
			);
		}

		u64 sel_begin = Max(sel.begin, range.begin);
		u64 sel_end   = Min(sel.end,   range.end);

		if (sel_begin < sel_end && is_active) {
			u64 local_begin = sel_begin - range.begin;
			u64 local_end   = sel_end   - range.begin;

			string before = line.range(0, local_begin);
			string middle = line.range(local_begin, local_end);

			f32 x0 = text_x + gfx_measure_text(before).x;
			f32 x1 = x0     + gfx_measure_text(middle).x;

			gfx_draw_quad({ x0, y, x1 - x0, line_h }, {}, cfg_color(selection), cfg_f32(radius));
		}

		gfx_draw_text(
			line_number,
			{rect.from.x + gutter_pad, y},
			current_line ? cfg_color(gutter_foreground) : cfg_color(gutter)
		);

		Line&          line_entry = buffer->line_tbl[i];
		u64 tok_begin = line_entry.token_begin;
		u64 tok_end   = tok_begin + line_entry.tokens_len;
		auto tok_slice = buffer->tokens.view().range(tok_begin, tok_end);

		vec4 rect = draw_line(buf_string, line, range.begin, tok_slice, {text_x, y});

		vec2 size = { rect.z, rect.w };

		if (current_line && ed_mode() != Ed_Mode::Command) {
			u64 cursor_offset = buffer->cursor - cursor_range.begin;

			string before_cursor = line.range(0, cursor_offset);

			f32 cursor_x = text_x + gfx_measure_text(before_cursor).x;

			vec2 cursor_pos = {cursor_x, y};

			if (ed_mode() != Ed_Mode::Insert) {
				gfx_draw_quad(
					{cursor_pos.x, cursor_pos.y, space_width, line_h},
					{},
					cfg_color(cursor),
					5.0f
				);

				if (cursor_offset < line.len) {
					int width = 0;
					utf8_decode(line.range(cursor_offset, line.len), &width);

					string cursor_char = line.range(cursor_offset, cursor_offset + width);
					gfx_draw_text(cursor_char, cursor_pos, cfg_color(cursor_text));
				}
			} else {
				gfx_draw_quad(
					{cursor_pos.x, cursor_pos.y, 2, line_h},
					{},
					cfg_color(cursor)
				);
			}
		}

		y += Max(size.y, line_h);
	}
}
