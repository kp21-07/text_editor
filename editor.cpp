#include "editor.h"
#include "config.h"

#include <stdio.h>

const string MODE_STRING[Mode_Count] = {
	S("normal"),
	S("insert"),
	S("command"),
};

typedef u32 Panel_Flags;
enum Panel_Flag : Panel_Flags {
	Panel_VSplit = 1 << 0, // HSplit otherwise
};

struct Panel {
	Panel_Flags flags;	

	Panel *parent;

	Panel *child1;
	Panel *chidl2;
};

global struct Editor
{
	Ed_Mode mode;
	bool should_exit;

	Buffer *active_buffer;

	Buffer *buffers;

	u64     buffer_count;
	u8      command[128];
	u64     command_length;
	
	Panel *panel_tree;

	/////////////////////
	// ~geb: memory management

	Arena *persist_arena;
	Arena *frame_arena;

	Arena     *buffer_arena;
	Buffer    *free_buffers;
} editor;


funcdef Ed_Cmd
ed__parse_command(string cmd)
{
	string striped = string_strip(cmd);

	string function = S("");
	Slice<string> args = {};

	{
		Slice<string> split = string_split(cmd, editor.frame_arena);
		if (split.len == 0) return {};

		function = split[0];
		if (split.len > 1) {
			args = slice(split, 1, split.len);
		}
	}

	if (string_equal(function, S("open")))
	{
		string path = S("");
		if (args.len > 0) {
			path = args[0];
			Ed_Cmd cmd = open_buffer(path);

			return cmd;
		}
	}

	if (string_equal(function, S("close"))) 
	{
		string path = S("");
		if (args.len > 0) {
			path = args[0];
		}

		Ed_Cmd cmd = close_buffer(path);
		return cmd;
	}

	if (string_equal(function, S("save"))) 
	{
		string path = S("");
		if (args.len > 0) {
			path = args[0];
		}

		Ed_Cmd cmd = save_buffer(path);

		return cmd;
	}

	if (string_equal(function, S("open_workspace")))
	{
		if (args.len > 0) {
			string path = args[0];
			Ed_Cmd cmd = open_workspace(path);
			return cmd;
		}
	}


	if (string_equal(function, S("exit")))
	{
		Ed_Cmd cmd = exit_editor();
		return cmd;
	}
	
	return {};
}

funcdef void
ed_init()
{
	MemZeroStruct(&editor);

	editor.persist_arena = arena_new(MB(8));
	editor.frame_arena = arena_new(MB(8));
	editor.buffer_arena = arena_new(GB(1));

	editor.mode = Mode_Normal;
}

funcdef void
ed_change_mode(Ed_Mode mode)
{
	if (mode == Mode_Insert)
	{
		if (ed_active_buffer() != nullptr)
			editor.mode = Mode_Insert;

		return;
	}

	editor.mode = mode;
}

funcdef bool
ed_update(Frame_Input input)
{
	auto buf = editor.active_buffer;
	rune c = input.character;
	string encoded_char = utf8_encode(c, editor.frame_arena);

	switch (editor.mode) {
		case Mode_Normal: {
			switch(c) {
				case ':': ed_change_mode(Mode_Command); break;
				case 'h': buffer_move_cursor(buf, 1, Direction_Left); break;
				case 'l': buffer_move_cursor(buf, 1, Direction_Right); break;
				case 'j': buffer_move_cursor(buf, 1, Direction_Down); break;
				case 'k': buffer_move_cursor(buf, 1, Direction_Up); break;

				case '0': ed_execute_cmd(jump_to_line_start()); break;
				case '-': ed_execute_cmd(jump_to_line_first_non_space()); break;
				case '=': ed_execute_cmd(jump_to_line_end()); break;

				case 'w': ed_execute_cmd(jump_to_word_start()); break;
				case 'e': ed_execute_cmd(jump_to_word_end()); break;
				case 'b': ed_execute_cmd(jump_to_word_previous()); break;

				case 'i': ed_change_mode(Mode_Insert); break;
				case 'a':
				{
					buffer_move_cursor(buf, 1, Direction_Right);
					ed_change_mode(Mode_Insert);
				} break;
				case 'I':
				{
					ed_execute_cmd(jump_to_line_first_non_space());
					ed_change_mode(Mode_Insert);
				} break;
				case 'A':
				{
					ed_execute_cmd(jump_to_line_end());
					ed_change_mode(Mode_Insert);
				} break;
				case 'o':
				{
					ed_execute_cmd(jump_to_line_end());
					buffer_insert(buf, S("\n"), ed_frame_arena());
					ed_change_mode(Mode_Insert);
				} break;
				case 'O':
				{

					ed_execute_cmd(jump_to_line_start());
					buffer_move_cursor(buf, 1, Direction_Left);
					buffer_insert(buf, S("\n"), ed_frame_arena());
					ed_change_mode(Mode_Insert);
				} break;
			}
		} break;

		case Mode_Command: 
		{
			u32 key_flags = input.key_flags;
			u8 input_char = (u8) c;

			if (key_flags & key_Escape) {
				editor.mode = Mode_Normal;
				editor.command_length = 0;
				break;
			} else if (key_flags & (key_Backspace | key_Delete)) {
				if (editor.command_length > 0) editor.command_length -= 1;
			} else if (c) {
				if (c == '\n') {

					string cmd_string = {
						editor.command, editor.command_length
					};

					Ed_Cmd cmd = ed__parse_command(cmd_string);
					ed_execute_cmd(cmd);

					editor.mode = Mode_Normal;
					editor.command_length = 0;
					// @TODO: parse cmd string and execute 
					break;
				} else if (c == '\t') {
					// nothing for now
				} else {
					if (editor.command_length < sizeof(editor.command)) {
						editor.command[editor.command_length] = input_char;
						editor.command_length += 1;
					}
				}
			}
		} break;

		case Mode_Insert: {
			if      (input.key_flags & key_Escape) ed_change_mode(Mode_Normal);
			else if (input.key_flags & key_Delete) buffer_delete(buf, 1, Direction_Right);
			else if (input.key_flags & key_Backspace) buffer_delete(buf, 1, Direction_Left);
			else if (c) {
				bool move_back = false;
				buffer_insert(ed_active_buffer(), encoded_char, ed_frame_arena());
				if (move_back) buffer_move_cursor(buf, 1, Direction_Left);
			}
		} break;

		default:
			break;
	}

	return editor.should_exit;
}


funcdef Buffer *
ed_active_buffer()
{
	return editor.active_buffer;
}


funcdef Ed_Mode
ed_mode()
{
	return editor.mode;
}

funcdef string
ed_command_as_string()
{
	string str = {
		editor.command,
		editor.command_length
	};

	return str;
}


funcdef Ed_Error
ed_execute_cmd(Ed_Cmd cmd)
{
	Ed_Error error = {};

	#define active(v)   Buffer *v = ed_active_buffer(); if (!v) break
	#define buf_str(b)  string_from_bytes(slice_from_list((b)->data))

	switch (cmd.kind)
	{
	case Cmd_None: break;

	case Cmd_Buffer_Open: {
		Buffer *buf;
		if (editor.free_buffers) {
			buf = (Buffer *)editor.free_buffers;
			editor.free_buffers = editor.free_buffers->next;
		} else {
			buf = alloc_struct(editor.buffer_arena, Buffer);
		}
		string path  = cmd.path;
		string input = S("");
		if (path.len) input = string_from_bytes(platform_load_entire_file(path, editor.frame_arena));
		buffer_make(buf, Max(input.len * 2, KB(512)), Max(string_count_lines(input) * 2, 2048), path);
		buf->next = editor.buffers;
		editor.buffers = buf;
		editor.buffer_count += 1;
		editor.active_buffer = buf;
		buffer_insert(buf, input, ed_frame_arena());
		buf->cursor = 0;
	} break;

	case Cmd_Buffer_Close: {
		string path = cmd.path;
		if (!path.len) {
			if (!editor.active_buffer) break;
			path = editor.active_buffer->path;
			if (!path.len) { error.kind = Ed_Error_Invalid_Argument; break; }
		}
		Buffer *last = nullptr;
		for (Buffer *buf = editor.buffers; buf; last = buf, buf = buf->next) {
			if (!string_equal(buf->path, path)) continue;
			(last ? last->next : editor.buffers) = buf->next;
			if (editor.active_buffer == buf) editor.active_buffer = editor.buffers;
			editor.buffer_count -= 1;
			buffer_deinit(buf);
			buf->next = editor.free_buffers;
			editor.free_buffers = buf;
			break;
		}
	} break;

	case Cmd_Buffer_Save: {
		active(buf);
		string path = cmd.path.len ? cmd.path : buf->path;
		if (!path.len) { error.kind = Ed_Error_Invalid_Argument; break; }
		if (!platform_save_entire_file(path, slice_from_list(buf->data), editor.frame_arena))
			error.kind = Ed_Error_Cmd_Failed;
	} break;

	case Cmd_Open_Workspace: {
		for (Buffer *buf = editor.buffers; buf; buf = buf->next) buffer_deinit(buf);
		arena_free(editor.buffer_arena);
		editor.free_buffers = nullptr;
		if (cmd.path.len) platform_change_cwd(cmd.path);
	} break;

	case Cmd_Jump_To_Line_Start: {
		active(buf);
		buffer_move_cursor_to(buf, buffer_line_range(buf, buffer_line_at_index(buf, buf->cursor)).begin);
	} break;

	case Cmd_Jump_To_Line_First_Non_Space: {
		active(buf);
		string data    = buf_str(buf);
		Range_U64 range = buffer_line_range(buf, buffer_line_at_index(buf, buf->cursor));
		u64 i = range.begin; int w = 0;
		while (i < range.end && is_space(utf8_decode(slice(data, i, range.end), &w))) i += w;
		buffer_move_cursor_to(buf, i);
	} break;

	case Cmd_Jump_To_Line_End: {
		active(buf);
		buffer_move_cursor_to(buf, buffer_line_range(buf, buffer_line_at_index(buf, buf->cursor)).end);
	} break;

	case Cmd_Jump_To_Word_Start: {
		active(buf);
		string data = buf_str(buf);

		u64 i = buf->cursor;
		int w = 0;

		if (i >= data.len) break;

		rune r = utf8_decode(slice(data, i, data.len), &w);
		Char_Kind kind = char_kind(r);

		while (i < data.len) {
			r = utf8_decode(slice(data, i, data.len), &w);

			if (char_kind(r) != kind) {
				break;
			}

			i += w;
		}

		while (i < data.len) {
			r = utf8_decode(slice(data, i, data.len), &w);

			if (char_kind(r) != Char_Space) {
				break;
			}

			i += w;
		}

		buffer_move_cursor_to(buf, i);
	} break;

	case Cmd_Jump_To_Word_End: {
		active(buf);

		string data = buf_str(buf);

		u64 i = buf->cursor;
		int w = 0;

		if (i >= data.len) break;

		rune r = utf8_decode(slice(data, i, data.len), &w);

		while (i < data.len && char_kind(r) == Char_Space) {
			i += w;

			if (i >= data.len) break;

			r = utf8_decode(slice(data, i, data.len), &w);
		}

		if (i >= data.len) break;

		Char_Kind kind = char_kind(r);

		u64 last = i;

		while (i < data.len) {
			r = utf8_decode(slice(data, i, data.len), &w);

			if (char_kind(r) != kind) {
				break;
			}

			last = i;
			i += w;
		}

		buffer_move_cursor_to(buf, last);
	} break;

	case Cmd_Jump_To_Word_Prev: {
		active(buf);

		string data = buf_str(buf);

		u64 i = buf->cursor;

		if (i == 0) break;

		i = utf8_prev_boundary(data, i);

		int w = 0;

		rune r = utf8_decode(slice(data, i, data.len), &w);

		while (i > 0 && char_kind(r) == Char_Space) {
			i = utf8_prev_boundary(data, i);
			r = utf8_decode(slice(data, i, data.len), &w);
		}

		Char_Kind kind = char_kind(r);

		while (i > 0) {
			u64 prev = utf8_prev_boundary(data, i);

			rune pr = utf8_decode(slice(data, prev, data.len), &w);

			if (char_kind(pr) != kind) {
				break;
			}

			i = prev;
		}

		buffer_move_cursor_to(buf, i);
	} break;

	case Cmd_Exit: {
		editor.should_exit = true;
	} break;

	default: 
		error.kind = Ed_Error_Invalid_Command;
		break;

	}

	#undef active
	#undef buf_str

	return error;
}

funcdef void
ed_handle_error(Ed_Error error)
{
	if (error.kind == Ed_Error_None) return;

	switch (error.kind)
	{
	default: break;
	}
}


funcdef Arena *
ed_persist_arnea()
{
	return editor.persist_arena;
}

funcdef Arena *
ed_frame_arena()
{
	return editor.frame_arena;
}

/////////////////////////////////////////////
// ~geb: commands

funcdef Ed_Cmd
open_buffer(string path)
{
	Ed_Cmd cmd = {};

	cmd.kind = Cmd_Buffer_Open;
	cmd.path = path;

	return cmd;
}


funcdef Ed_Cmd
close_buffer(string path)
{
	Ed_Cmd cmd = {};

	cmd.kind = Cmd_Buffer_Close;
	cmd.path = path;

	return cmd;
}

funcdef Ed_Cmd
save_buffer(string to_path)
{
	Ed_Cmd cmd = {};

	cmd.kind = Cmd_Buffer_Save;
	cmd.path = to_path;

	return cmd;
}


funcdef Ed_Cmd
open_workspace(string path)
{
	Ed_Cmd cmd = {};

	cmd.kind = Cmd_Open_Workspace;
	cmd.path = path;

	return cmd;
}


funcdef Ed_Cmd
jump_to_line_start()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Line_Start;
	return cmd;
}

funcdef Ed_Cmd jump_to_line_first_non_space()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Line_First_Non_Space;
	return cmd;
}

funcdef Ed_Cmd jump_to_line_end()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Line_End;
	return cmd;
}


funcdef Ed_Cmd
jump_to_word_start()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Word_Start;
	return cmd;
}

funcdef Ed_Cmd jump_to_word_end()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Word_End;
	return cmd;
}

funcdef Ed_Cmd jump_to_word_previous()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Word_Prev;
	return cmd;
}


funcdef Ed_Cmd
exit_editor()
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Exit;
	return cmd;
}
