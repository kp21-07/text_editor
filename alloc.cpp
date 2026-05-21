#include "editor.h"

#define PAGE_SIZE KB(4)

funcdef Arena *
arena_new(u64 reserve)
{
    u8 *mem = (u8 *)platform_mem_reserve(reserve);

    assert(mem && "failed to reserve arena backing memory");

    Arena *arena = (Arena *)mem;

	u64 commit_size = align_up_power_2(sizeof(Arena), PAGE_SIZE);
	assert(platform_mem_commit(mem, commit_size) && "failed to do initial arena commit");

    arena->reserved = { mem, reserve };
    arena->committed = commit_size;
    arena->used = sizeof(Arena);

    return arena;
}

funcdef void *
arena_alloc(Arena *arena, u64 size, u64 alignment)
{
    u64 current = align_up_power_2(arena->used, alignment);
    assert(size <= arena->reserved.len - current);

    u64 next = current + size;

    if (next > arena->committed) {
        u64 new_commit  = align_up_power_2(next, PAGE_SIZE);
        u64 diff        = new_commit - arena->committed;
        void *commit_end = &arena->reserved[arena->committed];

        assert(platform_mem_commit(commit_end, diff));
        arena->committed = new_commit;
    }

    void *result = &arena->reserved[current];
	arena->used  = next;

    return result;
}

funcdef void
arena_free(Arena *arena, u64 loc, bool rollback)
{
    assert(loc >= sizeof(Arena) && "arena_free: loc would overwrite arena header");
    assert(arena->used >= loc && "arena_free: loc is ahead of current used");
    arena->used = loc;

	if (rollback) {
		u64 new_commit = align_up_power_2(loc, PAGE_SIZE);
		if (new_commit < arena->committed) {
		
			void *decommit_start = &arena->reserved[new_commit];
			u64 decommit_size = arena->committed - new_commit;
			platform_mem_decommit(decommit_start, decommit_size);

			arena->committed = new_commit;
		}
	}
}
