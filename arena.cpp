#include "editor.h"

const u64 PAGE_SIZE = KB(4);

funcdef Arena *
arena_make(u64 reserve)
{
    void *mem = os_reserve(reserve);
    if (!mem) return nullptr;

    Arena *result     = (Arena *) mem;
    u64 commit_size   = Align_Up_Power_2(sizeof(Arena), PAGE_SIZE);
    assert(os_commit(mem, commit_size) && "failed to do initial arena commit");
    result->reserved  = reserve;
    result->committed = commit_size;
    result->used      = sizeof(Arena);

    ASAN_Poison((u8 *)result + sizeof(Arena), commit_size - sizeof(Arena));

    return result;
}

funcdef void
arena_delete(Arena *arena)
{
    if (arena == nullptr) return;
    ASAN_Unpoison(arena, arena->committed);
    os_release((void *) arena, arena->reserved);
}

funcdef void *
arena_allocate(Arena *arena, void *old_ptr, u64 old_size, u64 new_size, u64 alignment)
{
    assert(arena);
    if (new_size == 0)
        return nullptr;
    assert(new_size > old_size);

    if (old_ptr == nullptr) {
        u64 current = Align_Up_Power_2(arena->used, alignment);
        u64 next    = current + new_size;
        assert(next <= arena->reserved);
        if (next > arena->committed) {
            u64 new_commit   = Align_Up_Power_2(next, PAGE_SIZE);
            u64 diff         = new_commit - arena->committed;
            void *commit_ptr = (u8 *) arena + arena->committed;
            assert(os_commit(commit_ptr, diff));

            // newly committed pages come in unpoisoned from the OS;
            // poison them so the region stays consistent
            ASAN_Poison(commit_ptr, diff);
            arena->committed = new_commit;
        }
        void *result = (u8 *) arena + current;
        arena->used  = next;
        ASAN_Unpoison(result, new_size);
        return result;
    }

    u8 *base    = (u8 *) arena;
    u8 *old_end = (u8 *) old_ptr + old_size;
    if (base + arena->used == old_end) {
        u64 begin   = (u64) ((u8 *) old_ptr - base);
        u64 new_end = begin + new_size;
        assert(new_end <= arena->reserved);
        if (new_end > arena->committed) {
            u64 new_commit   = Align_Up_Power_2(new_end, PAGE_SIZE);
            u64 diff         = new_commit - arena->committed;
            void *commit_ptr = base + arena->committed;
            assert(os_commit(commit_ptr, diff));
            ASAN_Poison(commit_ptr, diff);
            arena->committed = new_commit;
        }

        // only unpoison the extension, old region is already accessible
        ASAN_Unpoison((u8 *)old_ptr + old_size, new_size - old_size);
        arena->used = new_end;
        return old_ptr;
    }

    // fallback: old_ptr was not the last allocation, copy forward
    void *new_ptr = arena_allocate(arena, nullptr, 0, new_size, alignment);
    memmove(new_ptr, old_ptr, old_size);
    ASAN_Poison(old_ptr, old_size); // stale pointer uses caught from here on
    return new_ptr;
}

funcdef void
arena_free(Arena *arena)
{
    void *start = (u8 *)arena + sizeof(Arena);
    u64   size  = arena->used - sizeof(Arena);
    ASAN_Poison(start, size);
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

    void *poison_start = (u8 *)temp.arena + temp.mark;
    u64   poison_size  = temp.arena->used - temp.mark;
    ASAN_Poison(poison_start, poison_size);

    temp.arena->used = temp.mark;
}

funcdef void
temp_rollback(Temp temp)
{
    assert(temp.arena);
    assert(temp.mark >= sizeof(Arena));
    assert(temp.mark <= temp.arena->used);

    void *poison_start = (u8 *)temp.arena + temp.mark;
    u64   poison_size  = temp.arena->used - temp.mark;
    ASAN_Poison(poison_start, poison_size);

    temp.arena->used = temp.mark;

    u64 desired_commit = Align_Up_Power_2(temp.arena->used, PAGE_SIZE);
    if (desired_commit < temp.arena->committed) {
        void *decommit_ptr = (u8 *)temp.arena + desired_commit;
        u64   decommit_size = temp.arena->committed - desired_commit;
        os_decommit(decommit_ptr, decommit_size);
        temp.arena->committed = desired_commit;
    }
}

// ~gaureesh @NOTE: used for temporary computations 
// that will be discarded after used. This type of 
// allocations will be extremely useful when we implement
// file handling and string handling functions.

//@TODO: make this into a 'thread context' object

global u64    SCRATCH_SIZE   = KB(512);
global Arena *global_scratch[2] = {
	arena_make(SCRATCH_SIZE),
	arena_make(SCRATCH_SIZE)
};

funcdef Arena *
scratch(Arena **conflicts, u64 count)
{
	for (u64 i=0; i< 2; ++i) {
		bool conflicting = false;
		for (u64 j=0; j<count; ++j) {
			if (global_scratch[i] == conflicts[j]) {
				conflicting = true;
				break;
			}
		}
		
		if (!conflicting)
			return global_scratch[i];
	}
	return nullptr;
}
