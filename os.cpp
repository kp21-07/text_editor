#include "editor.h"

#if OS_Linux
# include "linux/os_linux.cpp"
#elif OS_Windows
# include "win32/os_win32.cpp"
#else
# error "platform implementation missing"
#endif

#define RGFW_IMPLEMENTATION
#define RGFW_OPENGL
#define RGFW_NO_X11_CURSOR
#include "vendor/rgfw.h"

funcdef string
os_string(OS os)
{
	return OS_STRINGS[(u64) os];
}

funcdef OS_Handle
os_open_window(Arena *arena, string title)
{
	RGFW_glHints *hints = RGFW_getGlobalHints_OpenGL();
	hints->major = 3;
	hints->minor = 3;
	RGFW_setGlobalHints_OpenGL(hints);

	RGFW_window *win = alloc_struct(arena, RGFW_window);
	RGFW_createWindowPtr(
		(char *) title.raw, 0, 0, 1280, 800,
		RGFW_windowCenter | RGFW_windowAllowDND | RGFW_windowOpenGL,
		win
	);

	RGFW_window_makeCurrentContext_OpenGL(win);
	RGFW_window_swapInterval_OpenGL(win, 0);
	// RGFW_window_setMinSize(win, 200, 200);

	OS_Handle window_handle = {
		(uintptr_t) win
	};

	return window_handle;
}

funcdef void
os_close_window(OS_Handle window)
{
	RGFW_window *win = (RGFW_window *) window.v;
	RGFW_window_closePtr(win);
}

funcdef bool
os_window_should_close(OS_Handle window)
{
	return RGFW_window_shouldClose((RGFW_window *) window.v);
}

funcdef OS_Input
os_prepare_frame(OS_Handle window)
{
	RGFW_window *win = (RGFW_window *) window.v;

	RGFW_window_swapBuffers_OpenGL(win);

	OS_Input input = {};
	RGFW_event event = {};

	RGFW_waitForEvent(-1);
	while (RGFW_window_checkEvent(win, &event)) {
		switch (event.type) {
			case RGFW_windowRefresh:
			case RGFW_windowResized: {
				gfx_set_viewport(win->w, win->h);
				break;
			}

			case RGFW_keyPressed:  {
				RGFW_keyEvent k = event.key;
				switch(k.value){
					case RGFW_keyBackSpace: input.codepoint = '\b'; break;
					case RGFW_keyDelete: input.codepoint = '\x7F'; break;
					case RGFW_keyEscape: input.codepoint = '\x1b'; break;
					case RGFW_keyReturn: input.codepoint = '\n'; break;
					case RGFW_keyTab: input.codepoint = '\t'; break;
				}
				break;
			}

			case RGFW_keyChar: {
				RGFW_keyCharEvent k = event.keyChar;

				// if (!unicode_visual_rune(k.value))
				// 	break;

				input.codepoint = k.value;
				break;
			}
		}

	}

	return input;
}

funcdef ivec2
os_window_size(OS_Handle window)
{
	RGFW_window *win = (RGFW_window *) window.v;
	return {
		(s32) win->w,
		(s32) win->h
	};
}
funcdef void *
os_get_gl_proc_address()
{
	return (void *) RGFW_getProcAddress_OpenGL;
}


funcdef OS_TimeDuration
os_time_diff(OS_TimeStamp t0, OS_TimeStamp t1)
{
    u64 delta_ns = t1 - t0;

    OS_TimeDuration d;
    d.seconds      = (f64)delta_ns * 1e-9;
    d.milliseconds = (f64)delta_ns * 1e-6;
    d.microseconds = (f64)delta_ns * 1e-3;
    return d;
}

funcdef Load_Error
os_file_to_buffer(u8 *ptr, u64 cap, u64 *out, string path)
{
	Temp t = temp_begin(scratch(0, 0));
	defer(temp_end(t));

	string cstring = string_to_cstring(t.arena, path);

	FILE *f = fopen((char *) cstring.raw, "r");
	if (!f) {
		if (out)
			*out = 0;
		return Load_IO_Error;
	}
	defer(fclose(f));

    size_t len = fread(ptr, 1, cap, f);

    if (out) {
        *out = len;
    }

	return Load_Ok;
}

funcdef bool
os_write_to_file(string path, bytes data)
{
	Temp t = temp_begin(scratch(0, 0));
	defer(temp_end(t));

	string cstring = string_to_cstring(t.arena, path);

	FILE *f = fopen((char *)cstring.raw, "wb");
	if (!f) {
		return false;
    }
	defer(fclose(f));

    size_t written = 0;

    while (written < data.len) {
        size_t n = fwrite(data.raw + written, 1, data.len - written, f);

        if (n == 0) {
            break;
        }

        written += n;
    }

	return true;
}


funcdef bytes
os_load_entire_file(Arena *arena, string path)
{
	OS_FileData file_data = os_file_data(path);
	if (!Flag_Check(file_data.flags, File_Exists))
		return {};

	Temp t = temp_begin(arena);

	u64 arena_pos = arena->used;
	bytes data = alloc_slice(arena, u8, file_data.size);

	u64 out_len = 0;
	Load_Error err = os_file_to_buffer(data.raw, data.len, &out_len, path);

	if (err != Load_Ok) {
		temp_end(t);
		return {};
	}

	return data;
}

funcdef string
file_kind_string(OS_FileKind kind)
{
	switch (kind) {
	case OS_FileKind::C: case OS_FileKind::Cpp:
		return S("C/C++");
	case OS_FileKind::Text:
		return S("Text");
	case OS_FileKind::Bash:
		return S("Bash");
	case OS_FileKind::Config:
		return S("Config");
	default:
		return S("<Unknown>");
	}
}

funcdef string
os_get_clipboard_string(Arena *arena)
{
	u64 required = RGFW_readClipboardPtr(nullptr, 0);
	if (required == 0)
		return S("");

	bytes data = alloc_slice(arena, u8, required);
	RGFW_readClipboardPtr((char *)&data[0], data.len);

	return string_from_bytes(data);
}

funcdef void
os_set_clipboard_string(string str)
{
	RGFW_writeClipboard((char *) str.raw, str.len);
}
