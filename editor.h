#ifndef EDITOR_H
#define EDITOR_H

#define funcdef       static
#define global        static
#define local_persist static

#define force_inline inline __attribute__((always_inline))

#include <math.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

typedef uint8_t  u8;
typedef  int8_t  s8;
typedef uint16_t u16;
typedef  int16_t s16;
typedef uint32_t u32;
typedef  int32_t s32;
typedef uint64_t u64;
typedef  int64_t s64;

typedef float  f32;
typedef double f64;

static_assert(sizeof(f32) == 4, "float32 size missmatch");
static_assert(sizeof(f64) == 8, "float32 size missmatch");

#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))
#define Range_Check(min, val, max) ((min) <= (val) && (val) <= (max))

#define generic(...) template<typename __VA_ARGS__>

generic(T) struct Slice {
	T   *raw;
	u64  len;

	T& operator[](u64 index) {
		assert(index < len && "slice out of bounds error");
		return raw[index];
	}
};

generic(T) force_inline Slice<T>
slice(Slice<T> src, u64 begin, u64 end)
{
    assert(begin <= src.len);
    assert(end <= src.len);
    assert(begin <= end);
    return Slice<T> {
        .raw = src.raw + begin,
        .len = end - begin
    };
}

generic(T) struct List {
	T *raw;
	u64 len;
	u64 capacity;

	T& operator[](u64 index) {
		assert(index < len && "slice out of bounds error");
		return raw[index];
	}
};

generic(T) force_inline List<T>
list_from_buffer(Slice<T> buff)
{
	return List<T> {
		.raw = buff.raw,
		.len = 0,
		.capacity = buff.len
	};
}

generic(T) force_inline Slice<T>
slice_from_list(List<T> list) {
	return Slice<T> {
		.raw = list.raw,
		.len = list.len
	};
}

generic(T) force_inline void
append(List<T> *l, T value)
{
	assert(l && (l->len < l->capacity) && "fixed size list buffer overflow");

	l->raw[l->len] = value;
	l->len += 1;
}

generic(T) force_inline void
insert_slice(List<T> *l, u64 index, Slice<T> values)
{
	assert(index <= l->len && "insert index out of bounds");
	assert((l->len + values.len) <= l->capacity && "fixed size list buffer overflow");

	if (values.len == 0) {
		return;
	}

	T *dst = l->raw + index;

	memmove(
		dst + values.len,
		dst,
		(l->len - index) * sizeof(T)
	);

	memcpy(
		dst,
		values.raw,
		values.len * sizeof(T)
	);

	l->len += values.len;
}

generic(T) force_inline void
clear(List<T> *l)
{
	l->len = 0;
}


force_inline int
digit_count_u64(u64 n) {
    int count = 1;
    while (n >= 10) {
        n /= 10;
        count++;
    }
    return count;
}

/////////////////////////////////////////////////////////////////////////////////////
// ~geb: cursed defer from gb.h

template <typename T> struct RemoveReference       { typedef T Type; };
template <typename T> struct RemoveReference<T &>  { typedef T Type; };
template <typename T> struct RemoveReference<T &&> { typedef T Type; };

template <typename T> inline T &&forward(typename RemoveReference<T>::Type &t)  { return static_cast<T &&>(t); }
template <typename T> inline T &&forward(typename RemoveReference<T>::Type &&t) { return static_cast<T &&>(t); }
template <typename T> inline T &&move   (T &&t)                                 { return static_cast<typename RemoveReference<T>::Type &&>(t); }

template <typename F>
struct DeferImpl {
    F f;
    DeferImpl(F &&f) : f(forward<F>(f)) {}
    ~DeferImpl() { f(); }
};
template <typename F> DeferImpl<F> defer_func(F &&f) { return DeferImpl<F>(forward<F>(f)); }

#define TOKEN_PASTE(a, b) a##b
#define DEFER_NAME(base, line) TOKEN_PASTE(base, line)
#define defer(code) auto DEFER_NAME(_defer_, __LINE__) = defer_func([&]() { code; })

/////////////////////////////////////////////////////////////////////////////////////

typedef u32 rune;
typedef Slice<u8> bytes;
typedef Slice<const u8> string;
#define S(x) { .raw = (const u8 *) x, .len = (u64) (sizeof(x) - 1) }
#define s_fmt(s) (int) s.len, (char *) s.raw


force_inline string
string_from_bytes(bytes b) {
	return string {
		.raw = (const u8 *) b.raw,
		.len = b.len
	};
}

force_inline u64
align_up_power_2(u64 val, u64 alignment) {
	return (((val) + (alignment) - 1) & ~((alignment) - 1));
}

struct vec2 { f32 x, y; };
struct ivec2 { s32 x, y; };
struct vec4 { f32 x, y, z, w; };

struct Rect {
	vec2 from;
	vec2 size;
};

force_inline f32
smooth_move(f32 curr, f32 target, f32 sharpness, f32 dt) {
	return target + (curr - target) * expf(-sharpness * dt);
}

//
// alloc.cpp
//

struct Arena {
	bytes reserved;
	u64   committed;
	u64   used;

	// bytes data;
	// u64   used; 
};

#define KB(x) ((u64) x << 10)
#define MB(x) ((u64) x << 20)
#define GB(x) ((u64) x << 30)

funcdef Arena *arena_new(u64 reserve);
funcdef void *arena_alloc(Arena *arena, u64 size, u64 alignment = alignof(void *));
funcdef void arena_free(Arena *arena, u64 loc = sizeof(Arena), bool rollback = false);

#define alloc_struct(_arr, _T) (_T *) arena_alloc((_arr), sizeof(_T), alignof(_T))
#define alloc_slice(_arr, _T, _c) Slice<_T> { (_T *) arena_alloc((_arr), sizeof(_T) * (_c), alignof(_T)), (u64) (_c) }

#define Byte_Swap_U32(x) ((u32) __builtin_bswap32(x))
#define Hex(x) Byte_Swap_U32(x)

//
// strings.cpp
//

funcdef string string_concat(string a, string b, Arena *allocator);
funcdef Slice<string> string_as_lines(string parent, Arena *allocator);
funcdef string string_format(Arena *arena, const char *fmt, ...);

funcdef rune   utf8_decode(string slice, int *width);
funcdef string utf8_encode(rune cp, Arena *arena);
funcdef int    utf8_character_width(u8 first_byte);
#define        utf8_continuation_byte(b) (((b) & 0xC0) == 0x80)

funcdef bool unicode_visual_rune(rune r);

//
// graphics.cpp
//

enum : u32 {
	key_Backspace = 1u << 0,
	key_Delete    = 1u << 1,
	key_Escape    = 1u << 2,
};

struct Frame_Input {
	rune character;
	u32 key_flags;
};

struct Render_Clip {
	Rect rect;
	Render_Clip *next;
};

funcdef void graphics_init(const char *title, int width, int height, Arena *persist);
funcdef bool graphics_update(u32 color, Frame_Input *input);

funcdef void graphics_push_clip(Rect rect, Arena *frame_alloc);
funcdef Render_Clip graphics_pop_clip();

funcdef vec2 graphics_measure_text(string s);
funcdef f32 graphics_char_width(rune c);

funcdef u8 draw_push_layer(u8 new_layer);
funcdef void draw_quad(vec2 pos, vec2 size, u32 color = 0xFFFFFFFF, u8 texture = 0, vec2 uv0 = {0,0}, vec2 vec1 = {1,1}, ivec2 circ0 = {0}, ivec2 circ1 = {0});
funcdef vec2 draw_text(string s, vec2 start_pos, u32 color = 0xFFFFFFFF);
funcdef void draw_quad_rounded(vec2 pos, vec2 size, f32 radius, u32 color = 0xFFFFFFFF);
funcdef void draw_capsule(vec2 pos, vec2 size, u32 color = 0xFFFFFFFF);

#define push_draw_layer_scoped(layer) for(u8 _old_layer = draw_push_layer(layer), _i = 0; _i == 0; draw_push_layer(_old_layer), _i++)

//
// platform.cpp
//

struct Time_Duration {
	f64 seconds;
	f64 milliseconds;
	f64 microseconds;
};

funcdef bytes platform_load_entire_file(string path, Arena *allocator);

funcdef u64 platform_time_now();
funcdef Time_Duration platform_time_diff(u64 start, u64 end);

funcdef void *platform_mem_reserve(u64 size);
funcdef bool  platform_mem_commit(void *ptr, u64 size);
funcdef void  platform_mem_decommit(void *ptr, u64 size);
funcdef void  platform_mem_release(void *ptr, u64 size);

//
// buffer.cpp
//

struct Line {
	u64 begin;
};

struct Buffer 
{
	List<u8> data;
	List<Line> lines;

	u64 cursor;
};

struct Overflow
{
	u64 data_size;
	u64 line_count;
};

enum Direction {
	Direction_Right,
	Direction_Left,
	Direction_Up,
	Direction_Down,
};

funcdef void buffer_make(Buffer *buffer, bytes data, Slice<Line> line_table);
funcdef void buffer_insert(Buffer *buffer, string s, Overflow *overflow = nullptr);
funcdef void buffer_delete(Buffer *buffer, u64 count, Direction dir);
funcdef void buffer_move_cursor(Buffer *buffer, u64 count, Direction dir);
funcdef Slice<string> buffer_as_lines(Buffer *buffer, Arena *allocator);

//
// ui.cpp
//

typedef u32 UI_Box_Flags;

struct UI_Box {
	UI_Box *first;
	UI_Box *last;
	UI_Box *next;
	UI_Box *prev;
	UI_Box *parent;
};

enum UI_Draw_Kind : u32 {
	UI_Draw_Rect,
	UI_Draw_Text,
};

struct UI_Draw {
	UI_Draw_Kind kind;
};

struct UI_State {
	Arena *arena;
	Arena *build_arena;
	UI_Box *root;
};

#endif
