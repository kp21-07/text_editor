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

global struct {
	bool initialized;

	Arena *persist;

} os_ctx;

funcdef string
os_string(OS os)
{
	return OS_STRINGS[(u64) os];
}

funcdef void
os_init()
{
	if (os_ctx.initialized) return;
	os_ctx.persist = arena_make(KB(4));
	os_ctx.initialized = true;
}

funcdef void
os_deinit()
{
	arena_delete(os_ctx.persist);
}

funcdef OS_Handle
os_open_window(string title)
{
	os_init();

	RGFW_glHints *hints = RGFW_getGlobalHints_OpenGL();
	hints->major = 3;
	hints->minor = 3;
	RGFW_setGlobalHints_OpenGL(hints);

	RGFW_window *win = alloc_struct(os_ctx.persist, RGFW_window);
	RGFW_createWindowPtr(
		(char *) title.raw, 0, 0, 1280, 800,
		RGFW_windowCenter | RGFW_windowAllowDND | RGFW_windowOpenGL,
		win
	);

	RGFW_window_makeCurrentContext_OpenGL(win);
	RGFW_window_swapInterval_OpenGL(win, 0);
	RGFW_window_setMinSize(win, 200, 200);

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

	// RGFW_waitForEvent(-1);
	while (RGFW_window_checkEvent(win, &event)) {
		switch (event.type) {
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
				if (!unicode_visual_rune(k.value)) break;
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


funcdef bytes
os_load_entire_file(Arena *arena, string path)
{
	OS_FileData file_data = os_file_data(path);
	if (!Flag_Check(file_data.flags, File_Exists))
		return {};

	Temp t = temp_begin(arena);

	u64 arena_pos = arena->used;
	bytes data = alloc_slice(arena, u8, file_data.size);
	Load_Error err = os_file_to_buffer(data.raw, data.len, path);

	if (err != Load_Ok) {
		temp_end(t);
		return {};
	}

	return data;
}

funcdef string
os_path_canonical(Arena *arena, string path)
{
	Temp t0 = temp_begin(scratch());
	defer(temp_end(t0));

	auto temp_page = alloc_slice(scratch(), char, KB(4));

	u64 copy_len = Min(path.len, 4098 - 1);
	memcpy(temp_page.raw, path.raw, copy_len);
	temp_page[copy_len] = '\0';

	auto resolved = alloc_slice(scratch(), char, KB(4));
	if (!realpath(temp_page.raw, resolved.raw)) {
		return string_copy(arena, path);
	}

	return string_copy(arena, string{(u8 *) resolved.raw, strlen(resolved.raw)});
}
