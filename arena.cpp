#include "editor.h"

const u64 PAGE_SIZE = KB(4);

funcdef Arena *
arena_make(u64 reserve)
{
	void *mem = os_reserve(reserve);
	if (!mem) {
		return nullptr;
	}

	Arena *result = (Arena *) mem;

	u64 commit_size = Align_Up_Power_2(sizeof(Arena), PAGE_SIZE);
	assert(os_commit(mem, commit_size) && "failed to do initial arena commit");

	result->reserved = reserve;
	result->committed = commit_size;
	result->used = sizeof(Arena);
	
	return result;
}

funcdef void
arena_delete(Arena *arena) 
{
	if (arena == nullptr) return;

	void *mem = (void *) arena;
	os_release(mem, arena->reserved);
}

funcdef void *
arena_allocate(Arena *arena, void *old_ptr, u64 old_size, u64 new_size, u64 alignment)
{
	assert(arena);

	if (new_size == 0)
		return nullptr;

	assert(new_size > old_size);

	if (old_ptr == nullptr) {
		// this is a totally new allocations
		u64 current = Align_Up_Power_2(arena->used, alignment);
		u64 next    = current + new_size;

		assert(next <= arena->reserved);

		if (next > arena->committed) {
			u64 new_commit = Align_Up_Power_2(next, PAGE_SIZE);
			u64 diff       = new_commit - arena->committed;

			void *commit_ptr = (u8 *) arena + arena->committed;

			assert(os_commit(commit_ptr, diff));

			arena->committed = new_commit;
		}

		void *result = (u8 *) arena + current;

		arena->used = next;

		return result;
	}

	u8 *base    = (u8 *) arena;
	u8 *old_end = (u8 *) old_ptr + old_size;

	if (base + arena->used == old_end) {
		// this allocation was the last allocation
		// so we can just bump up the arena

		u64 begin   = (u64) ((u8 *) old_ptr - base);
		u64 new_end = begin + new_size;

		assert(new_end <= arena->reserved);

		if (new_end > arena->committed) {
			u64 new_commit = Align_Up_Power_2(new_end, PAGE_SIZE);
			u64 diff       = new_commit - arena->committed;

			void *commit_ptr = base + arena->committed;

			assert(os_commit(commit_ptr, diff));

			arena->committed = new_commit;
		}

		arena->used = new_end;

		return old_ptr;
	}

	// fallback realloc
	void *new_ptr = arena_allocate(arena, nullptr, 0, new_size, alignment);
	memmove(new_ptr, old_ptr, old_size);

	return new_ptr;
}

funcdef void
arena_free(Arena *arena)
{
	arena->used = sizeof(Arena);
}


funcdef Temp
temp_begin(Arena *arena)
{
	assert(arena);
	
	return Temp { arena, arena->used };
}

funcdef void
temp_end(Temp temp)
{
	if (!temp.arena) return;

	assert(temp.mark >= sizeof(Arena));
	assert(temp.mark <= temp.arena->used);
	
	temp.arena->used = temp.mark;
}

// ~gaureesh @NOTE: used for temporary computations 
// that will be discarded after used. This type of 
// allocations will be extremely useful when we implement
// file handling and string handling functions.

global u64    SCRATCH_SIZE   = MB(1);
global Arena *global_scratch = arena_make(SCRATCH_SIZE);

funcdef Arena *
scratch(Temp *temp)
{
	if (temp != nullptr) {
		*temp = temp_begin(global_scratch);
	}

	return global_scratch;
}
