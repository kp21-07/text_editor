#include "editor.h"

#if OS_Linux

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>

funcdef void *
os_reserve(u64 size)
{
	void *result = mmap(0, size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if(result == MAP_FAILED)
	{
		result = 0;
	}
	return result;
}

funcdef bool
os_commit(void *ptr, u64 size)
{
	mprotect(ptr, size, PROT_READ|PROT_WRITE);
	return 1;
}

funcdef void
os_decommit(void *ptr, u64 size)
{
	madvise(ptr, size, MADV_DONTNEED);
	mprotect(ptr, size, PROT_NONE);
}

funcdef void
os_release(void *ptr, u64 size)
{
	munmap(ptr, size);
}


funcdef OS_TimeStamp
os_time_now() 
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
}


funcdef OS_FileData
os_file_data(string path)
{
	OS_FileData result = {};
	
	Temp t = {};
	defer(temp_end(t));

	string cstr = string_to_cstring(scratch(&t), path);

	struct stat st;
	if (stat((char *)cstr.raw, &st) != 0) {
		return result;
	}

	result.flags |= File_Exists;

	if (S_ISDIR(st.st_mode)) {
		result.flags |= File_Directory;
	}

	if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
		result.flags |= File_Executable;
	}

	result.size = (u64)st.st_size;

	char const *ext = nullptr;
	for (char const *c = (char *)cstr.raw; *c; ++c) {
		if (*c == '.') {
			ext = c;
		}
	}

	if (ext) {
		if (strcasecmp(ext, ".c") == 0) {
			result.kind = OS_FileKind::C;
		}
		else if (strcasecmp(ext, ".cpp") == 0) {
			result.kind = OS_FileKind::Cpp;
		}
		else if (strcasecmp(ext, ".txt") == 0) {
			result.kind = OS_FileKind::Text;
		}
	}

	return result;
}


funcdef Load_Error
os_file_to_buffer(u8 *ptr, u64 len, string path)
{
	Temp t = temp_begin(scratch());
	defer(temp_end(t));

	string cstring = string_to_cstring(scratch(), path);
	
	int fd = open((char *)cstring.raw, O_RDONLY);
	if (fd < 0) {
		return Load_IO_Error;
	}
	defer(close(fd));

	struct stat st;
	if (fstat(fd, &st) < 0) 
		return Load_IO_Error;
	
	if (!S_ISREG(st.st_mode)) 
		return Load_IO_Error;

	u64 size = (u64)st.st_size;

	if (!ptr || len == 0) 
		return Load_Buffer_Overflow;

	u64 load_size = Min(size, len);
	u64 total = 0;

	while (total < load_size) {
		ssize_t n = read(fd, ptr + total, (size_t)(load_size - total));

		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return Load_IO_Error;
		}

		if (n == 0) { return Load_IO_Error; }
		total += (u64)n;
	}

	return (len >= size) ? Load_Ok : Load_Buffer_Overflow;
}

funcdef bool
os_write_to_file(string path, bytes data)
{
	Temp t = temp_begin(scratch());
	defer(temp_end(t));


	string cstring = string_to_cstring(scratch(), path);
	if (!cstring.raw)
		return false;

	mode_t mode = 0644;

	struct stat st;
	if (stat((char *)cstring.raw, &st) == 0)
		mode = st.st_mode & 0777;

	int fd = open((char *)cstring.raw, O_WRONLY | O_CREAT | O_TRUNC, mode);

	if (fd < 0)
		return false;

	defer(close(fd));

	u64 total = 0;
	while (total < data.len) {
		ssize_t n = write(fd, data.raw + total, (size_t)(data.len - total));

		if (n < 0) {
			if (errno == EINTR)
				continue;

			return false;
		}

		if (n == 0)
			return false;

		total += (u64)n;
	}

	return true;
}

funcdef void
os_set_working_dir(string dir)
{
	if (!dir.raw || !dir.len) return;

	local_persist char path[4098];
	u64 len = dir.len;

	if (len >= sizeof(path)) {
		len = sizeof(path) - 1;
	}

	memcpy(path, dir.raw, len);
	path[len] = '\0';

	chdir(path);
}

funcdef string
os_get_working_dir(Arena *arena)
{
	Temp t0 = temp_begin(scratch());
	defer(temp_end(t0));

	slice<char> temp_page = alloc_slice(scratch(), char, KB(4));
	getcwd(temp_page.raw, temp_page.len);

	string str = { (u8 *) temp_page.raw, strlen(temp_page.raw) };
	return string_copy(arena, str);
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

#elif OS_Windows

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <shlwapi.h>

funcdef void *
os_reserve(u64 size)
{
	void *result = VirtualAlloc(0, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
	return result; // NULL on failure – matches Linux behaviour
}
 
funcdef bool
os_commit(void *ptr, u64 size)
{
	void *p = VirtualAlloc(ptr, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE);
	return p != nullptr;
}
 
funcdef void
os_decommit(void *ptr, u64 size)
{
	VirtualFree(ptr, (SIZE_T)size, MEM_DECOMMIT);
}
 
funcdef void
os_release(void *ptr, u64 size)
{
	(void)size;
	VirtualFree(ptr, 0, MEM_RELEASE);
}

funcdef OS_TimeStamp
os_time_now()
{
	static LARGE_INTEGER freq = {};
	if (freq.QuadPart == 0)
		QueryPerformanceFrequency(&freq);
 
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
 
	u64 ticks = (u64)counter.QuadPart;
	u64 ns    = (u64)(ticks * 1000000000ULL / (u64)freq.QuadPart);
	return ns;
}

funcdef OS_FileData
os_file_data(string path)
{
	OS_FileData result = {};
 
	Temp t = {};
	defer(temp_end(t));
 
	string cstr = string_to_cstring(scratch(&t), path);
	if (!cstr.raw) return result;
 
	WIN32_FILE_ATTRIBUTE_DATA attrs;
	if (!GetFileAttributesExA((LPCSTR)cstr.raw, GetFileExInfoStandard, &attrs))
		return result;
 
	result.flags |= File_Exists;
 
	if (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		result.flags |= File_Directory;
 
	const char *ext = nullptr;
	for (const char *c = (const char *)cstr.raw; *c; ++c)
		if (*c == '.') ext = c;
 
	if (ext)
	{
		if (_stricmp(ext, ".exe") == 0 ||
		    _stricmp(ext, ".bat") == 0 ||
		    _stricmp(ext, ".cmd") == 0 ||
		    _stricmp(ext, ".com") == 0)
		{
			result.flags |= File_Executable;
		}
 
		if      (_stricmp(ext, ".c")   == 0) result.kind = OS_FileKind::C;
		else if (_stricmp(ext, ".cpp") == 0) result.kind = OS_FileKind::Cpp;
		else if (_stricmp(ext, ".txt") == 0) result.kind = OS_FileKind::Text;
	}
 
	ULARGE_INTEGER sz;
	sz.LowPart  = attrs.nFileSizeLow;
	sz.HighPart = attrs.nFileSizeHigh;
	result.size = (u64)sz.QuadPart;
 
	return result;
}
 
funcdef Load_Error
os_file_to_buffer(u8 *ptr, u64 len, string path)
{
	Temp t = temp_begin(scratch());
	defer(temp_end(t));
 
	string cstr = string_to_cstring(scratch(), path);
	if (!cstr.raw) return Load_IO_Error;
 
	HANDLE hFile = CreateFileA(
		(LPCSTR)cstr.raw,
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);
 
	if (hFile == INVALID_HANDLE_VALUE) return Load_IO_Error;
	defer(CloseHandle(hFile));
 
	DWORD type = GetFileType(hFile);
	if (type != FILE_TYPE_DISK) return Load_IO_Error;
 
	LARGE_INTEGER fileSz;
	if (!GetFileSizeEx(hFile, &fileSz)) return Load_IO_Error;
 
	u64 size = (u64)fileSz.QuadPart;
 
	if (!ptr || len == 0) return Load_Buffer_Overflow;
 
	u64 load_size = Min(size, len);
	u64 total     = 0;
 
	while (total < load_size)
	{
		DWORD to_read   = (DWORD)Min(load_size - total, (u64)0xFFFFFFFFULL);
		DWORD did_read  = 0;
 
		if (!ReadFile(hFile, ptr + total, to_read, &did_read, nullptr))
			return Load_IO_Error;
 
		if (did_read == 0) return Load_IO_Error; // unexpected EOF
 
		total += (u64)did_read;
	}
 
	return (len >= size) ? Load_Ok : Load_Buffer_Overflow;
}
 
funcdef bool
os_write_to_file(string path, bytes data)
{
	Temp t = temp_begin(scratch());
	defer(temp_end(t));
 
	string cstr = string_to_cstring(scratch(), path);
	if (!cstr.raw) return false;
 
	DWORD existing_attrs = GetFileAttributesA((LPCSTR)cstr.raw);
 
	HANDLE hFile = CreateFileA(
		(LPCSTR)cstr.raw,
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);
 
	if (hFile == INVALID_HANDLE_VALUE) return false;
	defer(CloseHandle(hFile));
 
	u64 total = 0;
	while (total < data.len)
	{
		DWORD to_write  = (DWORD)Min(data.len - total, (u64)0xFFFFFFFFULL);
		DWORD did_write = 0;
 
		if (!WriteFile(hFile, data.raw + total, to_write, &did_write, nullptr))
			return false;
 
		if (did_write == 0) return false;
 
		total += (u64)did_write;
	}
 
	return true;
}
 

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



