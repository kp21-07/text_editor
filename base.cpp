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

	u64 i=0;
	while(i < s.len)
	{
        h ^= p[i];
        h *= 1099511628211ULL;
		i += 1;
	}

    return h;
}

funcdef slice<string>
fuzzy_filter(slice<string> src, string key, Arena *arena)
{
	if (!src.len)
		return {};
	if (!key.len)
		return src;

	list<string> matches = list_make(alloc_slice(arena, string, src.len));

	u8    key_lower[256];
	u64   key_mask = 0;

	for (u64 k = 0; k < key.len; ++k) {
		u8 c = key[k] | 0x20;
		key_lower[k] = c;
		key_mask |= (1ULL << (c & 63));
	}

	for (u64 i = 0; i < src.len; ++i) {
		string itr = src[i];
		if (itr.len < key.len)
			continue;

		u64 itr_mask = 0;
		for (u64 j = 0; j < itr.len; ++j)
			itr_mask |= (1ULL << ((itr[j] | 0x20) & 63));
		if ((itr_mask & key_mask) != key_mask)
			continue;

		u64 ki = 0;
		for (u64 j = 0; j < itr.len && ki < key.len; ++j) {
			u8 c = itr[j] | 0x20;
			if (c == key_lower[ki])
				++ki;
		}
		if (ki == key.len)
			append(&matches, itr);
	}

	return slice<string> { matches.raw, matches.len };
}

////////////////
// big array

template<typename T> funcdef big_array<T>
big_array_make(u64 capacity)
{
	big_array<T> result = {};

	u64 reserve = Align_Up_Power_2(sizeof(Arena), alignof(T));
	reserve += capacity * sizeof(T);

	result.arena = arena_make(reserve);
	result.len   = 0;

	return result;
}

template<typename T> funcdef void
big_array_delete(big_array<T> *array)
{
	arena_delete(array->arena);
	*array = {};
}


template<typename T> funcdef void
big_array_push(big_array<T> *array, T value)
{
	T *slot = alloc_struct(array->arena, T);
	*slot = value;
	array->len += 1;
}

template<typename T> funcdef void
big_array_pop(big_array<T> *array, u64 index)
{
    assert(index <= array->len);

    u64 offset = Align_Up_Power_2(sizeof(Arena), alignof(T));

    array->len = index;

    array->arena->used = offset + index * sizeof(T);
}
