#include "editor.h"

global struct {
	Arena *persist_arena;
	Arena *frame_arena;
	Arena *modal_arena;

	Ed_Mode mode;

	Arena  *workspace_arena;
	Buffer *active_buffer;
	string  working_dir;

	list<u8> cmd_string;
	Buffer_Map buffer_map;
} ed_ctx;


funcdef void
ed_init()
{
	ed_ctx.persist_arena = arena_make(MB(4));
	ed_ctx.frame_arena   = arena_make(MB(4));
	ed_ctx.modal_arena   = arena_make(MB(4));
	ed_ctx.workspace_arena = arena_make(MB(4));

	ed_ctx.working_dir = os_get_working_dir(ed_ctx.workspace_arena);
	ed_ctx.buffer_map = buffer_map_make(ed_ctx.workspace_arena, 128);
	ed_ctx.active_buffer = nullptr;
}

funcdef void
ed_deinit()
{
	arena_delete(ed_ctx.workspace_arena);
	arena_delete(ed_ctx.modal_arena);
	arena_delete(ed_ctx.frame_arena);
	arena_delete(ed_ctx.persist_arena);
}

funcdef Ed_Mode
ed_mode() 
{
	return ed_ctx.mode;
}


funcdef Buffer *
ed_active()
{
	return ed_ctx.active_buffer;
}

funcdef string
ed_directory()
{
	return ed_ctx.working_dir;
}

funcdef string
modal_string(Ed_Mode mode)
{
	switch (mode)
	{
		case Ed_Mode::Normal:  return S("normal");
		case Ed_Mode::Insert:  return S("insert");
		case Ed_Mode::Command: return S("command");
	}

	return {};
}


funcdef string
ed_command_string()
{
	return ed_ctx.cmd_string.view();
}

funcdef slice<string>
ed_command_strings(Arena *arena)
{
	return string_split(ed_ctx.cmd_string.view(), arena);
}

funcdef void
ed_exec_command(Ed_Cmd cmd)
{
    Ed_CmdKind kind = cmd.kind;
    switch (kind) {
		case Cmd_Buffer_Open: {
			slice<string> args = cmd.arg_strings;
			for (u64 i = 0; i < args.len; ++i) {
				string path = string_copy(ed_ctx.workspace_arena, args[i]);
				OS_FileData file_data = os_file_data(path);

				if (file_data.flags & File_Directory)
					continue;

				Buffer *buf = buffer_map_get(&ed_ctx.buffer_map, path);

				if (buf == nullptr) {
					Buffer new_buf = {};
					buffer_init(&new_buf, path);
					buf = buffer_map_insert(&ed_ctx.buffer_map, new_buf);
				}

				if (i == args.len - 1)
					ed_ctx.active_buffer = buf;
			}
		} break;

		case Cmd_Buffer_Close:
		{
			slice<string> paths = cmd.arg_strings;
			bool closed_active = false;

			if (ed_active()) {
				for (u64 i = 0; i < paths.len; ++i) {
					if (string_equal(paths[i], ed_ctx.active_buffer->path)) {
						closed_active = true;
						break;
					}
				}
			}

			for (u64 i = 0; i < paths.len; ++i)
			{
				string path = paths[i];
				if (string_equal(path, S("*")))
				{

					return;
				}
				buffer_map_remove(&ed_ctx.buffer_map, paths[i]);
			}

			if (!closed_active)
				break;

			ed_ctx.active_buffer = nullptr;

			slice<Buffer> table = ed_ctx.buffer_map.table;
			for (u64 i = 0; i < table.len; ++i) {
				if (Flag_Check(table[i].flags, Buffer_Occupied)) {
					ed_ctx.active_buffer = &table[i];
					break;
				}
			}
		} break;

        case Cmd_Buffer_Save:
        {
			Buffer *active = ed_active();
			if (!active)
				break;

			string data = active->data.view();
			string path = cmd.arg_string.len ? cmd.arg_string : active->path;

			if (path.len) 
			{
				bytes data_b = {
					(u8 *) data.raw,
					data.len
				};
				bool ok = os_write_to_file(path, data_b);
			}
        } break;

		case Cmd_Mode_Change:
		{
			arena_free(ed_ctx.modal_arena);	

			switch(cmd.arg_mode) {
				case Ed_Mode::Command:
					ed_ctx.cmd_string = list_make(
						alloc_slice(ed_ctx.modal_arena, u8, 128)
					);
				default: break;
			}

			clear(&ed_ctx.cmd_string);
			ed_ctx.mode = cmd.arg_mode;
		} break;

		case Cmd_Cursor_Move:
			buffer_move_cursor(ed_ctx.active_buffer, cmd.arg_u64, cmd.arg_dir);
			break;

		case Cmd_Insert_String:
			if (ed_mode() == Ed_Mode::Command) {
				bytes input_bytes = {(u8 *)cmd.arg_string.raw, cmd.arg_string.len};
				append_slice(&ed_ctx.cmd_string, input_bytes);
			} else {
				buffer_insert(ed_ctx.active_buffer, cmd.arg_string);
			}
			break;

		case Cmd_Delete_String:
			if (ed_mode() == Ed_Mode::Command) {
				if (ed_ctx.cmd_string.len > 0)	
					ed_ctx.cmd_string.len -= 1;
			} else {
				buffer_delete(ed_ctx.active_buffer, cmd.arg_u64, cmd.arg_dir);
			}
			break;

		case Cmd_Jump_To_Line:
		{
			Buffer *buf = ed_active();
			u64 line_index = Min(cmd.arg_u64, buf->lines.len);
			if (line_index > 0)
				line_index -= 1;

			auto range  = buffer_line_range(buf, line_index);
			buffer_move_cursor(buf, range.begin, Direction::Absolute);
		} break;

        case Cmd_Workspace_Open:
        {
			string path = cmd.arg_string;
			os_set_working_dir(path);

			buffer_map_clear(&ed_ctx.buffer_map);

			arena_free(ed_ctx.workspace_arena);

			ed_ctx.working_dir = os_get_working_dir(ed_ctx.workspace_arena);
			ed_ctx.buffer_map = buffer_map_make(ed_ctx.workspace_arena, 128);
			ed_ctx.active_buffer = nullptr;
        } break;

        case Cmd_Exit:
        {
        } break;

        default: break;
    }
}

funcdef Arena *
frame_arena()
{
	return ed_ctx.frame_arena;
}

funcdef Arena *
persist_arena()
{
	return ed_ctx.persist_arena;
}

//////////////////////////////////


funcdef Ed_Cmd
open_workspace(string path)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Workspace_Open;
	cmd.arg_string = path;
	return cmd;
}

funcdef Ed_Cmd
open_buffer(slice<string> paths)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Buffer_Open;
	cmd.arg_strings = paths;
	return cmd;
}

funcdef Ed_Cmd
close_buffer(slice<string> paths)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Buffer_Close;
	cmd.arg_strings = paths;
	return cmd;
}

funcdef Ed_Cmd
save_buffer(string to)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Buffer_Save;
	cmd.arg_string = to;
	return cmd;
}

funcdef Ed_Cmd
change_mode(Ed_Mode to)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Mode_Change;
	cmd.arg_mode = to;
	return cmd;
}

funcdef Ed_Cmd
move_cursor(Direction dir, u64 count)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Cursor_Move;
	cmd.arg_dir = dir;	
	cmd.arg_u64 = count;
	return cmd;
}


funcdef Ed_Cmd
insert_string(string str)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Insert_String;
	cmd.arg_string = str;
	return cmd;
}


funcdef Ed_Cmd
delete_string(Direction dir, u64 count)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Delete_String;
	cmd.arg_dir = dir;	
	cmd.arg_u64 = count;
	return cmd;
}

funcdef Ed_Cmd
jump_to_line(u64 line)
{
	Ed_Cmd cmd = {};
	cmd.kind = Cmd_Jump_To_Line;
	cmd.arg_u64 = line;
	return cmd;
}


funcdef string
cmd_function(Ed_CmdKind kind)
{
	switch (kind) {
		case Cmd_Buffer_Open:  return S("open");
		case Cmd_Buffer_Close: return S("close");
		case Cmd_Buffer_Save:  return S("save");
		case Cmd_Exit:         return S("exit");
		default:
			return S("");
	}
}
