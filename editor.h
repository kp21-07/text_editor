#ifndef EDITOR_H
#define EDITOR_H

//////////
// ~gaureesh @NOTE: base types

#include <stdint.h> // for fixed size integers.
#include <assert.h> // for assert macro.
#include <string.h> // for memmove, memcpy etc. ( not for cstd string functions ).
#include <stdarg.h> // for va_args.
#include <stdio.h>  // standard io operations.
#include <stddef.h>
#include <math.h>

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

template<typename T>
struct slice {
	T   *raw;
	u64  len;
	
	slice<T> range(u64 begin, u64 end) {
		assert(begin <= len);
		assert(begin <= end);
		assert(end <= len);
		
		return {
			raw + begin,
			end - begin
		};
	}

	T& operator[](u64 index) {
		assert(index < this->len);
	return this->raw[index];
	}
};

template<typename T>
struct list {
	T   *raw;
	u64  len;
	u64  capacity;

	T& operator[](u64 index) {
		assert(index < this->len);
		return this->raw[index];
	}
	
	slice<const T> view() {
		return { 
			raw,
			len
		};
	}
};

struct vec2 { f32 x, y; };
struct ivec2 { s32 x, y; };
struct vec4 { f32 x, y, z, w; };

struct Rect {
	vec2 from;
	vec2 size;
};

struct Range_u64 {
	u64 begin, end;
};

typedef slice<u8> bytes;
typedef slice<const u8> string;
typedef u32 rune;

#define S(x) string { (const u8 *) (x), sizeof(x) - 1 }
#define S_FMT(s) (int) s.len, (char *) s.raw

//////////
// ~gaureesh @NOTE: os and compiler detection

#ifdef _WIN32
# define OS_Windows 1
# define OS_Linux   0
# define OS_Mac     0
#elif __linux__
# define OS_Windows 0
# define OS_Linux   1
# define OS_Mac     0
#elif __APPLE__
# define OS_Windows 0
# define OS_Linux   0
# define OS_Mac     1
#else
# error "target platform unsupported"
#endif

#if defined(__clang__)
# define Compiler_Clang
#elif defined(__GNUC__) || defined(__GNUG__)
# define Compiler_GCC
#elif defined(_MSC_VER)
# define Compiler_CL
#else
# error "c++ compiler not supported"
#endif

#ifndef __cplusplus
# error "compiler should be c++"
#endif

//////////////
// ~gaureesh @NOTE: useful macro definitions

#define funcdef       static
#define local_persist static
#define global        static

#define Max(a, b) ((a) > (b) ? (a) : (b))
#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Clamp(min, val, max)  Min(Max((val), (min)), (max))

#define Lerp(a, b, t) ((a) + ((b) - (a)) * (t));

#define Align_Up_Power_2(val, alignment) (((val) + (alignment) - 1) & ~((alignment) - 1));
#define MemZeroStruct(s) memset(s, 0, sizeof(*s))

#define KB(x) (u64) (x << 10)
#define MB(x) (u64) (x << 20)
#define GB(x) (u64) (x << 30)

#define Flag_Check(__flags, __mask) !!((__flags) & (__mask))
#define Flag_Set(__flags, __mask) ((__flags) |= (__mask))
#define Flag_Remove(__flags, __mask) ((__flags) &= ~(__mask))

#define byteswap_u32(x) (((x & 0x000000FFu) << 24) | \
                        ((x & 0x0000FF00u) <<  8) | \
                        ((x & 0x00FF0000u) >>  8) | \
                        ((x & 0xFF000000u) >> 24))

#define Hex(x) byteswap_u32(x)

template<typename T> funcdef list<T> list_make(slice<T> buf);
template<typename T> funcdef void append(list<T> *l, T value);
template<typename T> funcdef void append_slice(list<T> *l, slice<T> values);
template<typename T> funcdef void insert_slice(list<T> *l, u64 index, slice<T> values);
template<typename T> funcdef void clear(list<T> *l);

//////////////
// ~gaureesh @NOTE: cursed `defer` construct for c++

template<typename T> struct RemoveReference       { typedef T Type; };
template<typename T> struct RemoveReference<T &>  { typedef T Type; };
template<typename T> struct RemoveReference<T &&> { typedef T Type; };

template<typename T> inline T &&forward(typename RemoveReference<T>::Type &t)  { return static_cast<T &&>(t); }
template<typename T> inline T &&forward(typename RemoveReference<T>::Type &&t) { return static_cast<T &&>(t); }
template<typename T> inline T &&move   (T &&t)                                 { return static_cast<typename RemoveReference<T>::Type &&>(t); }

template<typename F>
struct DeferImpl {
    F f;
    DeferImpl(F &&f) : f(forward<F>(f)) {}
    ~DeferImpl() { f(); }
};
template<typename F> DeferImpl<F> defer_func(F &&f) { return DeferImpl<F>(forward<F>(f)); }

#define TOKEN_PASTE(a, b) a##b
#define DEFER_NAME(base, line) TOKEN_PASTE(base, line)
#define defer(code) auto DEFER_NAME(_defer_, __LINE__) = defer_func([&]() { code; })


inline int
digit_count_u64(u64 n) {
    int count = 1;
    while (n >= 10) {
        n /= 10;
        count++;
    }
    return count;
}


//////////////
// ~gaureesh @NOTE: arena

struct Arena {
	u64   reserved;
	u64   committed;
	u64   used;
};

struct Temp {
	Arena *arena;
	u64    mark;
};

funcdef Arena *arena_make(u64 reserve);
funcdef void   arena_delete(Arena *arena);
funcdef void  *arena_allocate(Arena *arena, 
                              void *old_ptr,
                              u64 old_size,
                              u64 new_size,
                              u64 alignment);
funcdef void   arena_free(Arena *arena);

funcdef Temp   temp_begin(Arena *arena);
funcdef void   temp_end(Temp temp);

funcdef Arena *scratch(Temp *temp = nullptr);

#define alloc_struct(_arr, _T) (_T *) arena_allocate((_arr), nullptr, 0, sizeof(_T), alignof(_T))
#define alloc_slice(_arr, _T, _n) slice<_T> { (_T *) arena_allocate((_arr), nullptr, 0, sizeof(_T) * (_n), alignof(_T)), (u64) (_n) }
#define realloc_slice(_arr, _s, _n) { (decltype((_s).raw)) arena_allocate((_arr), (_s).raw, sizeof(*(_s).raw) * (_s).len, sizeof(*(_s).raw) * (_n), alignof(decltype(*(_s).raw))), (u64)(_n) }

template<typename T> funcdef void list_realloc(list<T> *list, u64 new_cap, Arena *arena); // defined in base.cpp

//////////////
// ~gaureesh @NOTE: os

struct OS_Handle { uintptr_t v; };

struct OS_Input {
	rune codepoint;
};

enum class OS : s32 {
	Windows,
	Linux,
	Mac,
	Count,

#if OS_Windows
	Current = OS::Windows,
#elif OS_Linux
	Current = OS::Linux,
#elif OS_MAC
	Current = OS::Mac,
#endif
};

const string OS_STRINGS[(u32) OS::Count] = {
	S("Windows"),
	S("Linux"),
	S("Mac"),
};

funcdef void os_init();
funcdef void os_deinit();

funcdef void *os_reserve(u64 size);
funcdef bool  os_commit(void *ptr, u64 size);
funcdef void  os_decommit(void *ptr, u64 size);
funcdef void  os_release(void *ptr, u64 size);

funcdef OS_Handle os_open_window(string title);
funcdef void os_close_window(OS_Handle window);
funcdef bool os_window_should_close(OS_Handle window);
funcdef OS_Input os_prepare_frame(OS_Handle window);
funcdef ivec2 os_window_size(OS_Handle window);

funcdef string os_string(OS os);
funcdef void *os_get_gl_proc_address();

typedef u64 OS_TimeStamp; // in nanoseconds
struct OS_TimeDuration {
	f64 seconds;
	f64 milliseconds;
	f64 microseconds;
};

funcdef OS_TimeStamp os_time_now();
funcdef OS_TimeDuration os_time_diff(OS_TimeStamp t0, OS_TimeStamp t1);

typedef u32 OS_FileFlags;
enum OS_FileFlag {
	File_Directory  = 1 << 0,
	File_Executable = 1 << 1,
	File_Exists     = 1 << 2,
};

enum class OS_FileKind : u32 {
	Unknown,

	C,
	Cpp,
	Text,
};

struct OS_FileData {
	OS_FileFlags flags;
	OS_FileKind kind;
	u64 size;
};

enum Load_Error {
	Load_Ok,
	Load_Not_Found,
	Load_Access_Denied,
	Load_Invalid_Path,
	Load_Buffer_Overflow,
	Load_IO_Error, // for all other type of errors
	Load_Error_Count,
};

funcdef OS_FileData os_file_data(string path);
funcdef Load_Error  os_file_to_buffer(u8 *ptr, u64 len, string path);
funcdef bytes       os_load_entire_file(Arena *arena, string path);
funcdef bool        os_write_to_file(string path, bytes data);

funcdef void os_set_working_dir(string dir);
funcdef string os_get_working_dir(Arena *arena);
funcdef string os_path_canonical(Arena *arena, string path);


//////////////
// ~gaureesh @NOTE: string

enum CharKind {
	Char_Open,
	Char_Quote,
	Char_Close,
	Char_Symbol,
	Char_Number,
	Char_Letter,
};

funcdef CharKind char_kind(rune r);
funcdef rune     char_get_pair(rune r);

funcdef s64    string_to_int(string s, bool *ok);
funcdef string string_strip(string s);
funcdef string string_copy(Arena *arena, string s);
funcdef string string_from_bytes(bytes data);
funcdef string string_concat(Arena *arena, string a, string b);
funcdef string string_format(Arena *arena, const char *fmt_string, ...);
funcdef string string_from_cstring(Arena *arena, char *cstring);
funcdef string string_to_cstring(Arena *arena, string s);
funcdef string string_from_list(list<u8> data);
funcdef u64    string_count_lines(string s);
funcdef u64    string_column_count(string s, int indent_width);
funcdef bool   string_equal(string a, string b);

funcdef slice<string> string_split(string original, Arena *arena);
funcdef slice<string> strings_from_cstrings(Arena *arena, int count, char **cstrings);

funcdef rune   utf8_decode(string slice, int *width);
funcdef string utf8_encode(rune cp, Arena *arena);
funcdef u64    utf8_prev_boundary(string data, u64 i);
funcdef u64    utf8_next_boundary(string data, u64 i);
funcdef int    utf8_character_width(u8 first_byte);
#define        utf8_continuation_byte(b) (((b) & 0xC0) == 0x80)
funcdef bool   is_space(rune r);
funcdef bool   unicode_visual_rune(rune r);


//////////////
// ~gaureesh @NOTE: gfx

enum : u16 {
	Texture_White,
	Texture_Font,
	Texture_Count
};

struct Render_Clip {
	Rect rect;
	Render_Clip *next;
};

funcdef void gfx_init(OS_Handle window, Arena *persist);
funcdef void gfx_deinit();

funcdef void gfx_begin();
funcdef void gfx_submit();

funcdef void gfx_set_viewport(s32 width, s32 height);

funcdef f32  delta_time();
funcdef f32  gfx_line_height();
funcdef f32  gfx_char_width(rune c);
funcdef vec2 gfx_measure_text(string s);

funcdef void gfx_push_clip(Rect rect, Arena *frame_alloc);
funcdef Render_Clip gfx_pop_clip();

funcdef void draw_quad(vec2 pos, vec2 size, u32 color, u8 texture = Texture_White, vec2 uv0 = {0.5f,0.5f}, vec2 uv1 = {0.5f,0.5f}, ivec2 circ0 = {0,0}, ivec2 circ1 = {0,0});
funcdef vec2 draw_text(string s, vec2 start_pos, u32 color);
funcdef void draw_quad_rounded(vec2 pos, vec2 size, f32 radius, u32 color);
funcdef void draw_capsule(vec2 pos, vec2 size, u32 color);

//////////////
// ~gaureesh @NOTE: ui

enum UI_SizeKind: u32 {
	Size_Fixed,
	Size_Fit,
	Size_Fill,
	Size_Percent,
};

struct UI_SizeAxis {
	UI_SizeKind kind;
	f32 value;
};

struct UI_Size {
	UI_SizeAxis w, h;
};

struct UI_Padding {
	u16 top, right, bottom, left;
};

#define Pad(n) UI_Padding { n, n, n, n }
#define Pad_XY(x, y) UI_Padding { y, x, y, x }

enum UI_Layout: u8 {
	Layout_Col,
	Layout_Row,
};

enum UI_Align: u8 {
	Align_Start,
	Align_Center,
	Align_End,
};

typedef u32 UI_Flags;
enum UI_Flag : UI_Flags {
	UI_Invisible     = 1 << 0,
	UI_Clip_Children = 1 << 1,
};

struct UI_Config {
	UI_Flags   flags;
	UI_Size    size;
	UI_Padding padding;

	f32        radius;
	f32        border;

	u32        color;
	u32        text_color;
	u32        border_color;

	string     text;

	u16        gap;
	UI_Layout  layout;
	UI_Align   align;
};

struct UI_Box {
	UI_Box   *parent;
	UI_Box   *sibling;
	UI_Box   *first;
	UI_Box   *last;
	UI_Config config;
	Rect      rect;
};

funcdef void ui_init(Arena *frame_arena);

funcdef UI_Box *ui_open(UI_Config config);
funcdef void    ui_close();

funcdef void ui_begin_frame(Rect rect, UI_Config frame_config);
funcdef void ui_end_frame();
funcdef void ui_draw();

#define UI(_cfg) for (UI_Box *__this_box__ = ui_open(_cfg); __this_box__; ui_close(), __this_box__ = nullptr)

// ~geb: components

funcdef void ui_text(string text, u32 color, UI_Align alignment = Align_Start, UI_SizeKind x_kind = Size_Fixed );

funcdef void ui_hr(u32 color, UI_SizeAxis size = { Size_Fill, 1.0f }, f32 thick = 1);

//////////////
// ~gaureesh @NOTE: buffer

struct Line {
	u64 index;
};

typedef u64 Buffer_Flags;
enum Buffer_Flag : Buffer_Flags
{
    Buffer_Occupied  = 1 << 0,
    Buffer_Dirty     = 1 << 1,
};

struct Buffer {
	Arena *arena;
	string path;

	list<u8> data;
	list<Line> lines;

	u64 cursor;
	u64 desired_col;

	// -- meta data --

	Buffer_Flags flags;
	OS_FileKind file_kind;

	// -- visual info -- 

	f32 scroll_y;
	f32 target_scroll_y;
};

enum class Direction {
	Absolute,
	Left,
	Right,
	Down,
	Up,
};

funcdef Load_Error buffer_init(Buffer *buffer, string path);
funcdef void       buffer_deinit(Buffer *buffer);
funcdef u64        buffer_line_index_at(Buffer *buffer, u64 buf_index);
funcdef Range_u64  buffer_line_range(Buffer *buffer, u64 line_index);
funcdef string     buffer_slice(Buffer *buffer, Arena *arena, Range_u64 rang);
funcdef void       buffer_insert(Buffer *buffer, string s);
funcdef void       buffer_delete(Buffer *buffer, u64 count, Direction direction);
funcdef void       buffer_move_cursor(Buffer *buf, u64 amount, Direction dir);
funcdef rune       buffer_char_at(Buffer *buf, s64 index);
funcdef u64        buffer_cursor(Buffer *buf);

struct Buffer_Map {
	slice<Buffer> table;
	u64 count;
};

funcdef Buffer_Map buffer_map_make(Arena *arena, u64 capacity);
funcdef void       buffer_map_clear(Buffer_Map *map);
funcdef Buffer    *buffer_map_insert(Buffer_Map *map, const Buffer& buffer);
funcdef Buffer    *buffer_map_get(Buffer_Map *map, string path);
funcdef bool       buffer_map_remove(Buffer_Map *map, string path);
funcdef slice<string> buffer_map_get_paths(Buffer_Map *map, Arena *arena);

funcdef void draw_buffer_view(Buffer *buffer, Rect rect);

//////////////
// ~gaureesh @NOTE: editor

enum class Ed_Mode {
	Normal,
	Insert,
	Command,
};

enum Ed_CmdKind {
	Cmd_None,

	Cmd_In_Palette_Begin,

	Cmd_Buffer_Open,
	Cmd_Buffer_Close,
	Cmd_Buffer_Save,
	Cmd_Exit,

	Cmd_In_Palette_End,



	Cmd_Mode_Change,
	Cmd_Cursor_Move,
	Cmd_Insert_String,
	Cmd_Delete_String,
	Cmd_Jump_To_Line,
	Cmd_Workspace_Open,
	Cmd_Count,
};

struct Buf_Handle {
	u64 v;
};

struct Ed_Cmd {
	Ed_CmdKind kind;

	string arg_string;
	Ed_Mode arg_mode;
	Direction arg_dir;
	u64 arg_u64;
	slice<string> arg_strings;
};


funcdef void ed_init();
funcdef void ed_deinit();

funcdef void ed_exec_command(Ed_Cmd command);
funcdef string ed_command_string();
funcdef slice<string> ed_command_strings(Arena *arena);

funcdef Ed_Mode ed_mode();
funcdef Buffer *ed_active();
funcdef string ed_directory();

funcdef Arena *frame_arena();
funcdef Arena *persist_arena();

funcdef string  modal_string(Ed_Mode mode);

// ~gauresesh @NOTE: commands

funcdef Ed_Cmd open_workspace(string path);
funcdef Ed_Cmd open_buffer(slice<string> paths);
funcdef Ed_Cmd close_buffer(slice<string> paths);
funcdef Ed_Cmd save_buffer(string to = S(""));
funcdef Ed_Cmd change_mode(Ed_Mode to);
funcdef Ed_Cmd move_cursor(Direction dir, u64 count);
funcdef Ed_Cmd insert_string(string str);
funcdef Ed_Cmd delete_string(Direction dir, u64 count);
funcdef Ed_Cmd jump_to_line(u64 line);

funcdef string cmd_function(Ed_CmdKind kind);


#endif
