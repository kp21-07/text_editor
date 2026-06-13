#include "../editor.h"

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <libgen.h>

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
	
	Temp t = temp_begin(scratch(0, 0));
	defer(temp_end(t));

	string cstr = string_to_cstring(t.arena, path);
	char const *ext = nullptr;
	for (char const *c = (char *)cstr.raw; *c; ++c) {
		if (*c == '.') {
			ext = c;
		}
	}

	if (ext) {
		if (strcasecmp(ext, ".c") == 0 || strcasecmp(ext, ".h") == 0) {
			result.kind = OS_FileKind::C;
		}
		else if (strcasecmp(ext, ".cpp") == 0 || strcasecmp(ext, ".hpp") == 0) {
			result.kind = OS_FileKind::Cpp;
		}
		else if (strcasecmp(ext, ".sh") == 0) {
			result.kind = OS_FileKind::Bash;
		}
		else if (strcasecmp(ext, ".txt") == 0) {
			result.kind = OS_FileKind::Text;
		}
		else if (strcasecmp(ext, ".data") == 0) {
			result.kind = OS_FileKind::Config;
		}
	}

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

	int result = chdir(path);
}

funcdef string
os_get_working_dir(Arena *arena)
{
	Temp t0 = temp_begin(scratch(&arena, 1));
	defer(temp_end(t0));

	slice<char> temp_page = alloc_slice(t0.arena, char, KB(4));
	char *cwd = getcwd(temp_page.raw, temp_page.len);

	string str = { (u8 *) cwd, strlen(cwd) };
	return string_copy(arena, str);
}

funcdef string
os_path_canonical(Arena *arena, string path)
{
	Temp t0 = temp_begin(scratch(&arena, 1));
	defer(temp_end(t0));

	auto temp_page = alloc_slice(t0.arena, char, KB(4));

	u64 copy_len = Min(path.len, 4098 - 1);
	memcpy(temp_page.raw, path.raw, copy_len);
	temp_page[copy_len] = '\0';

	auto resolved = alloc_slice(t0.arena, char, KB(4));
	if (!realpath(temp_page.raw, resolved.raw)) {
		return string_copy(arena, path);
	}

	return string_copy(arena, string{(u8 *) resolved.raw, strlen(resolved.raw)});
}


funcdef string
os_get_exec_directory(Arena *arena)
{
	Temp t0 = temp_begin(scratch(&arena, 1));
	defer(temp_end(t0));

	slice<char> temp_page = alloc_slice(t0.arena, char, KB(4));
	int count = readlink("/proc/self/exe", temp_page.raw, temp_page.len - 1);
	temp_page[count] = '\0';

	char *dir = dirname(temp_page.raw);

	string str = { (u8 *) dir, (u64) strlen(dir) };
	return string_copy(arena, str);
}
