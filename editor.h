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
#define Clamp(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

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
        src.raw + begin,
        end - begin
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
		buff.raw,
		0,
		buff.len
	};
}

generic(T) force_inline Slice<T>
slice_from_list(List<T> list) {
	return Slice<T> {
		list.raw,
		list.len
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
#define S(x) { (const u8 *) x, (u64) (sizeof(x) - 1) }
#define s_fmt(s) (int) s.len, (char *) s.raw

#define Byte_Swap_U32(x) ((u32) __builtin_bswap32(x))
#define Hex(x) Byte_Swap_U32(x)


force_inline rune
get_closing_char(rune c) {
	switch (c) {
		case '(': return ')';
		case '{': return '}';
		case '[': return ']';
		case '\'': return '\'';
		case '"': return '"';
	}
	return '\0';
}

force_inline rune
get_opening_char(rune c) {
	switch (c) {
		case ')': return '(';
		case '}': return '{';
		case ']': return '[';
		case '\'': return '\'';
		case '"': return '"';
	}
	return '\0';
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

struct Range_U64 {
	u64 begin;
	u64 end;
};

force_inline f32
smooth_move(f32 curr, f32 target, f32 sharpness, f32 dt) {
	return target + (curr - target) * expf(-sharpness * dt);
}

//
// alloc.cpp
//

struct Free_Node {
	bytes data;
	Free_Node *next;
};

struct Arena {
	bytes reserved;
	u64   committed;
	u64   used;
};

#define KB(x) ((u64) x << 10)
#define MB(x) ((u64) x << 20)
#define GB(x) ((u64) x << 30)

#define MemZeroStruct(s) memset(s, 0, sizeof(*s))

funcdef Arena *arena_new(u64 reserve);
funcdef void arena_delete(Arena *arena);
funcdef void *arena_alloc(Arena *arena, u64 size, u64 alignment);
funcdef void *arena_realloc(Arena *arena, void *old_ptr, u64 old_size, u64 new_size, u64 alignment);
funcdef void arena_free(Arena *arena, u64 loc = sizeof(Arena), bool rollback = false);

#define alloc_struct(_arr, _T) (_T *) arena_alloc((_arr), sizeof(_T), alignof(_T))
#define alloc_slice(_arr, _T, _c) Slice<_T> { (_T *) arena_alloc((_arr), sizeof(_T) * (_c), alignof(_T)), (u64) (_c) }
#define realloc_slice(_arr, _T, _old, _count) \
	Slice<_T> { \
		(_T *) arena_realloc((_arr), (_old).raw, sizeof(_T) * (_old).len, sizeof(_T) * (_count), alignof(_T)), \
		(_count) \
	}

//
// strings.cpp
//

force_inline string
string_from_bytes(bytes b) {
	return string {
		(const u8 *) b.raw,
		b.len
	};
}

enum Char_Kind {
	Char_Space,
	Char_Word,
	Char_Punct,
};

funcdef string string_format(Arena *arena, const char *fmt, ...);
funcdef u64 string_count_lines(string s);
funcdef u64 string_column_count(string s, int indent_width = 4);
funcdef string string_concat(string a, string b, Arena *arena);
funcdef u64 string_find_first(string s, rune r);
funcdef u64 string_find_last(string s, rune r);
funcdef string string_strip(string s);
funcdef Slice<string> string_split(string original, Arena *arena);
funcdef bool string_equal(string a, string b);
funcdef string string_copy(string str, Arena *arena);

funcdef Char_Kind char_kind(rune r);
funcdef rune   utf8_decode(string slice, int *width);
funcdef string utf8_encode(rune cp, Arena *arena);
funcdef u64    utf8_prev_boundary(string data, u64 i);
funcdef int    utf8_character_width(u8 first_byte);
#define        utf8_continuation_byte(b) (((b) & 0xC0) == 0x80)
funcdef bool   is_space(rune r);

funcdef bool unicode_visual_rune(rune r);

//
// graphics.cpp
//

enum : u16 {
	Texture_White,
	Texture_Font,
	Texture_Count
};

enum : u8 {
	Draw_Layer_Base,
	Draw_Layer_Popup,
};

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
funcdef bool graphics_update(Frame_Input *input);
funcdef void graphics_submit_draw();
funcdef vec2 graphics_resolution();
funcdef f32  graphics_line_height();

funcdef void graphics_push_clip(Rect rect, Arena *frame_alloc);
funcdef Render_Clip graphics_pop_clip();

funcdef vec2 graphics_measure_text(string s);
funcdef f32 graphics_char_width(rune c);

funcdef u8 draw_push_layer(u8 new_layer);
funcdef void draw_quad(vec2 pos, vec2 size, u32 color = 0xFFFFFFFF, u8 texture = 0, vec2 uv0 = {0,0}, vec2 vec1 = {1,1}, ivec2 circ0 = {0,0}, ivec2 circ1 = {0,0});
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
funcdef bool platform_save_entire_file(string path, bytes data, Arena *scratch);
funcdef void platform_change_cwd(string dir);

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
	u64 index;
};

struct Buffer 
{
	Arena *arena;
	string path;

	List<u8> data;
	List<Line> lines;

	u64 cursor;
	u64 desired_column;

	Buffer *next;
};

enum Direction {
	Direction_Right,
	Direction_Left,
	Direction_Up,
	Direction_Down,
};

funcdef void buffer_make(Buffer *buffer, u64 data_cap, u64 line_count, string path);
funcdef void buffer_deinit(Buffer *buffer);
funcdef void buffer_insert(Buffer *buffer, string s, Arena *scratch);
funcdef void buffer_delete(Buffer *buffer, u64 count, Direction dir);
funcdef void buffer_move_cursor(Buffer *buffer, u64 count, Direction dir);
funcdef void buffer_move_cursor_to(Buffer *buffer, u64 index);
funcdef Slice<string> buffer_as_lines(Buffer *buffer, Arena *allocator);
funcdef u64  buffer_line_at_index(Buffer *buffer, u64 array_index);
funcdef Range_U64 buffer_line_range(Buffer *buffer, u64 line_index);

//
// editor.cpp
//

enum Ed_Mode {
	Mode_Normal,
	Mode_Insert,
	Mode_Command,
	Mode_Count
};

enum Ed_Cmd_Kind {
	Cmd_None,
	Cmd_Buffer_Open,
	Cmd_Buffer_Close,
	Cmd_Buffer_Save,

	Cmd_Jump_To_Line_Start,
	Cmd_Jump_To_Line_First_Non_Space,
	Cmd_Jump_To_Line_End,

	Cmd_Jump_To_Word_Start,
	Cmd_Jump_To_Word_End,
	Cmd_Jump_To_Word_Prev,

	Cmd_Open_Workspace,

	Cmd_Exit,
};

struct Ed_Cmd {
	Ed_Cmd_Kind kind;

	/////////// buffer ///////////

	string path;
};

enum Ed_Error_Kind : u32 {
	Ed_Error_None,
	Ed_Error_Invalid_Command,
	Ed_Error_Invalid_Argument,
	Ed_Error_Cmd_Failed,
};

struct Ed_Error {
	Ed_Error_Kind kind;
};

funcdef void ed_init();
funcdef bool ed_update(Frame_Input input);
funcdef void ed_change_mode(Ed_Mode mode);
funcdef Buffer *ed_active_buffer();
funcdef Ed_Mode ed_mode();
funcdef string ed_command_as_string();

funcdef Ed_Error ed_execute_cmd(Ed_Cmd cmd);
funcdef void ed_handle_error(Ed_Error error);

funcdef Arena *ed_persist_arnea();
funcdef Arena *ed_frame_arena();

// commands

funcdef Ed_Cmd open_buffer(string path);
funcdef Ed_Cmd close_buffer(string path);
funcdef Ed_Cmd save_buffer(string to_path);
funcdef Ed_Cmd open_workspace(string path);
funcdef Ed_Cmd jump_to_line_start();
funcdef Ed_Cmd jump_to_line_first_non_space();
funcdef Ed_Cmd jump_to_line_end();
funcdef Ed_Cmd jump_to_word_start();
funcdef Ed_Cmd jump_to_word_end();
funcdef Ed_Cmd jump_to_word_previous();
funcdef Ed_Cmd exit_editor();

#endif


