#include "editor.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>

funcdef bytes
platform_load_entire_file(string path, Arena *allocator)
{
	Slice<char> cpath = alloc_slice(allocator, char, path.len + 1);
	if (!cpath.raw) return {};

	for (u64 i = 0; i < path.len; i++) {
		cpath[i] = (char)path.raw[i];
	}
	cpath[path.len] = 0;

	int fd = open(cpath.raw, O_RDONLY);
	if (fd < 0) {
		return {};
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return {};
	}

	u64 size = (u64) st.st_size;

	bytes data= alloc_slice(allocator, u8, size);
	if (!data.raw) {
		close(fd);
		return {};
	}

	u64 total = 0;
	while (total < size) {
		ssize_t n = read(fd, data.raw + total, size - total);
		if (n <= 0) {
			close(fd);
			return {};
		}
		total += (u64)n;
	}

	close(fd);

	return data;
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
