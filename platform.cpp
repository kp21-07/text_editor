#include "editor.h"

#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>

funcdef Load_Error
platform_file_info(string path, File_Info *info, Arena *scratch) {
	string cstring = string_to_cstring(path, scratch);

	if (!cstring.raw || !info)
		return Load_Invalid_Path;

	struct stat st;

	if (lstat((char *) cstring.raw, &st) != 0) {
		switch (errno) {
			case ENOENT:  return Load_Not_Found;
			case EACCES:  return Load_Access_Denied;
			case EINVAL:  return Load_Invalid_Path;
			default:      return Load_IO_Error;
		}
	}

    info->size  = (u64)st.st_size;
    info->flags = 0;

	if (S_ISDIR (st.st_mode)) info->flags |= File_Directory;
	if (S_ISLNK (st.st_mode)) info->flags |= File_Symlink;
	if (S_ISBLK (st.st_mode) ||
		S_ISCHR (st.st_mode)) info->flags |= File_Device;
	if (S_ISFIFO(st.st_mode)) info->flags |= File_Pipe;
	if (S_ISSOCK(st.st_mode)) info->flags |= File_Socket;

    if (access((char *)cstring.raw, R_OK) == 0) info->flags |= File_Readable;
    if (access((char *)cstring.raw, W_OK) == 0) info->flags |= File_Writable;
    if (access((char *)cstring.raw, X_OK) == 0) info->flags |= File_Executable;

	const char *base = strrchr((char *) cstring.raw, '/');
	base = base ? base + 1 : (char *) cstring.raw;

	if (base[0] == '.')
		info->flags |= File_Hidden;

	return Load_Ok;
}


funcdef Load_Error
platform_file_to_buffer(string path, u8 *ptr, u64 len, Arena *scratch)
{
	string cstring = string_to_cstring(path, scratch);

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


funcdef bytes
platform_load_entire_file(string path, Arena *arena, Arena *scratch)
{
	File_Info info = {};
	
	Load_Error err = platform_file_info(path, &info, scratch);
	if (err != Load_Ok) {
		return {};
	}

	u64 arena_pos = arena->used;
	bytes data = alloc_slice(arena, u8, info.size);
	err = platform_file_to_buffer(path, data.raw, data.len, scratch);

	if (err != Load_Ok) {
		arena_free(arena, arena_pos);
		return {};
	}

	return data;
}

funcdef bool
platform_save_entire_file(string path, bytes data, Arena *scratch)
{
	string cstring = string_to_cstring(path, scratch);
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


funcdef string
platform_path_canonical(Arena *arena, string path)
{
	static char tmp[4098];
	u64 copy_len = Min(path.len, 4098 - 1);
	memcpy(tmp, path.raw, copy_len);
	tmp[copy_len] = '\0';

	char resolved[4098];
	if (!realpath(tmp, resolved)) {
		return string_copy(path, arena);
	}
	return string_copy(string{(u8 *) resolved, strlen(resolved)}, arena);
}

funcdef bool
platform_is_directory(string path, Arena *scratch)
{
	Slice<char> cpath =  alloc_slice(scratch, char, path.len + 1);

	memcpy(cpath.raw, path.raw, path.len);
	cpath[path.len] = 0;

	struct stat path_stat;

	if (stat(cpath.raw, &path_stat) < 0) {
		return false;
	}

	return S_ISDIR(path_stat.st_mode);
}

funcdef void
platform_set_current_working_directory(string dir)
{
	if (!dir.raw || !dir.len) return;

	static char path[4098];
	u64 len = dir.len;

	if (len >= sizeof(path)) {
		len = sizeof(path) - 1;
	}

	memcpy(path, dir.raw, len);
	path[len] = '\0';

	chdir(path);
}

funcdef string
platform_get_current_working_dir(Arena *allocator)
{
	char *cwd = getcwd(nullptr, 0);
	defer(free(cwd));

	if (!cwd) {
		return {};
	}

	string cstring = { (u8 *) cwd, strlen(cwd) };
	string result = string_copy(cstring, allocator);

	return result;
}

funcdef u64
platform_time_now() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (u64)ts.tv_sec * 1000000ULL +
	       (u64)(ts.tv_nsec / 1000ULL);
}

funcdef Time_Duration
platform_time_diff(u64 start, u64 end) {
	u64 us = end - start;

	Time_Duration result = {};

	result.microseconds = (f64)us;
	result.milliseconds = (f64)us / 1000.0;
	result.seconds      = (f64)us / 1000000.0;

	return result;
}


funcdef void *
platform_mem_reserve(u64 size)
{
	void *result = mmap(0, size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if(result == MAP_FAILED)
	{
		result = 0;
	}
	return result;
}

funcdef bool
platform_mem_commit(void *ptr, u64 size)
{
	mprotect(ptr, size, PROT_READ|PROT_WRITE);
	return 1;
}

funcdef void
platform_mem_decommit(void *ptr, u64 size)
{
	madvise(ptr, size, MADV_DONTNEED);
	mprotect(ptr, size, PROT_NONE);
}

funcdef void
platform_mem_release(void *ptr, u64 size)
{
	munmap(ptr, size);
}


