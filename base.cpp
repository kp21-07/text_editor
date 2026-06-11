#include "editor.h"

// ~gauresh @NOTE: base type procedure implementations

template<typename T>
funcdef list<T>
list_make(slice<T> buf) {
	return {
		buf.raw,
		0,
		buf.len
	};
}

template<typename T>
funcdef void
append(list<T> *l, T value)
{
	assert(l);
	assert((l->len < l->capacity) && "fixed size list buffer overflow");

	l->raw[l->len] = value;
	l->len += 1;
}

template<typename T>
funcdef void
append_slice(list<T> *l, slice<T> values)
{
	assert(l);
	assert((l->len + values.len) <= l->capacity && "fixed size list buffer overflow");

	if (values.len == 0) {
		return;
	}

	memcpy(l->raw + l->len, values.raw, values.len * sizeof(T));

	l->len += values.len;
}

template<typename T>
funcdef void
insert_slice(list<T> *l, u64 index, slice<T> values)
{
	assert(l);
	assert(index <= l->len && "insert index out of bounds");
	assert((l->len + values.len) <= l->capacity && "fixed size list buffer overflow");

	if (values.len == 0) {
		return;
	}

	T *dst = l->raw + index;

	memmove(dst + values.len, dst, (l->len - index) * sizeof(T));
	memcpy(dst, values.raw, values.len * sizeof(T));

	l->len += values.len;
}

template<typename T>
funcdef void
clear(list<T> *l)
{
	l->len = 0;
}


template<typename T>
funcdef void
list_realloc(list<T> *list, u64 new_cap, Arena *arena) {
	assert(new_cap > list->capacity);
	slice<T> old_data = {
		list->raw,
		list->capacity
	};

	slice<T> new_data = realloc_slice(arena, old_data, new_cap);
	list->raw = new_data.raw;
	list->capacity = new_data.len;
}


funcdef u64
hash_string(string s)
{
    u64 h = 14695981039346656037ULL; 

    const u8 *p = (const u8 *)s.raw;

    for (size_t i = 0; i < s.len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }

    return h;
}

