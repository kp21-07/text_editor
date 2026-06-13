#include "../editor.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <shlwapi.h>
#include <direct.h>

funcdef void *
os_reserve(u64 size)
{
	void *result = VirtualAlloc(0, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
	return result; // NULL on failure - matches Linux behaviour
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
 
	Temp t = temp_begin(scratch(0, 0));
	defer(temp_end(t));
 
	string cstr = string_to_cstring(t.arena, path);
	if (!cstr.raw) return result;
	const char *ext = nullptr;
	for (const char *c = (const char *)cstr.raw; *c; ++c)
		if (*c == '.') ext = c;
 
	if (ext)
	{
		if (_stricmp(ext, ".exe") == 0 ||
		    _stricmp(ext, ".bat") == 0 ||
		    _stricmp(ext, ".cmd") == 0 ||
		    _stricmp(ext, ".com") == 0) {
			result.flags |= File_Executable;
		}
 
		if      (_stricmp(ext, ".c")   == 0 || _stricmp(ext, ".h")   == 0) result.kind = OS_FileKind::C;
		else if (_stricmp(ext, ".cpp") == 0 || _stricmp(ext, ".hpp") == 0) result.kind = OS_FileKind::Cpp;
		else if (_stricmp(ext, ".sh") == 0) result.kind = OS_FileKind::Bash;
		else if (_stricmp(ext, ".txt") == 0) result.kind = OS_FileKind::Text;
		else if (_stricmp(ext, ".data") == 0) result.kind = OS_FileKind::Config;
	}
 
	WIN32_FILE_ATTRIBUTE_DATA attrs;
	if (!GetFileAttributesExA((LPCSTR)cstr.raw, GetFileExInfoStandard, &attrs))
		return result;
 
	result.flags |= File_Exists;
 
	if (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		result.flags |= File_Directory;
 
	ULARGE_INTEGER sz;
	sz.LowPart  = attrs.nFileSizeLow;
	sz.HighPart = attrs.nFileSizeHigh;
	result.size = (u64)sz.QuadPart;
 
	return result;
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

	_chdir(path);
}

funcdef string
os_get_working_dir(Arena *arena)
{
	Temp t0 = temp_begin(scratch(&arena, 1));
	defer(temp_end(t0));

	slice<char> temp_page = alloc_slice(t0.arena, char, KB(4));

	if (!_getcwd(temp_page.raw, (int)temp_page.len)) {
		return {};
	}

	string str = {
		(u8 *)temp_page.raw,
		strlen(temp_page.raw),
	};

	return string_copy(arena, str);
}


funcdef string
os_path_canonical(Arena *arena, string path)
{
    Temp t0 = temp_begin(scratch(&arena, 1));
    defer(temp_end(t0));

    auto temp_page = alloc_slice(t0.arena, char, KB(4));

    u64 copy_len = Min(path.len, KB(4) - 1);
    memcpy(temp_page.raw, path.raw, copy_len);
    temp_page[copy_len] = '\0';

    auto resolved = alloc_slice(scratch(), char, KB(4));

    if (!_fullpath(resolved.raw, temp_page.raw, resolved.len)) {
        return string_copy(arena, path);
    }

    return string_copy(
        arena,
        string{(u8 *)resolved.raw, strlen(resolved.raw)}
    );
}
