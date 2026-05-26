#include "editor.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>


funcdef int
utf8_character_width(u8 first_byte)
{
	local_persist int widths[256] = {
		// ascii control characters
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

		// ascii characters
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

		// continuation bytes
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,

		3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
		4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0,
	};

	return widths[first_byte];
}

funcdef bool
unicode_visual_rune(rune r)
{
	/* ASCII controls */
	if (r < 0x20 || r == 0x7F)
		return false;

	/* C1 controls */
	if (r >= 0x80 && r <= 0x9F)
		return false;

	/* surrogate halves */
	if (r >= 0xD800 && r <= 0xDFFF)
		return false;

	/* invalid Unicode range */
	if (r > 0x10FFFF)
		return false;

	return true;
}


funcdef Char_Kind
char_kind(rune r)
{
	// @TODO: utf8 aware

	if (is_space(r)) {
		return Char_Space;
	}

	switch (r) {
		case '(':
		case '{':
		case '[':
			return Char_Open;

		case ')':
		case '}':
		case ']':
			return Char_Close;

		case '"':
		case '\'':
		case '`':
			return Char_Quote;

		case '@': case '!': case '#': case '$':
		case '^': case '%': case '&': case '*':
		case '-': case '+': case '=': case '|':
		case '\\': case '/': case ':': case ';':
		case ',': case '.': case '?': case '~':
			return Char_Punct;
	}

	return Char_Word;
}


funcdef rune
char_get_pair(rune r)
{
	switch (r) {
		case '(': return ')';
		case ')': return '(';

		case '{': return '}';
		case '}': return '{';

		case '[': return ']';
		case ']': return '[';

		case '<': return '>';
		case '>': return '<';

		case '"': return '"';
		case '\'': return '\'';
		case '`': return '`';
	}

	return 0;
}

funcdef rune
utf8_decode(string slice, int *width)
{
	if (!slice.len) {
		if (width) *width = 0;
		return 0;
	}

	const u8 *s = slice.raw;
	u8  b0 = s[0];

	int w = utf8_character_width(b0);

	if (width) *width = w;

	if (w == 0 || slice.len < (u64)w) {
		if (width) *width = 1;
		return 0xFFFD;
	}

	switch (w) {
		case 1: {
			return b0;
		} break;

		case 2: {
			u8 b1 = s[1];

			if ((b1 & 0xC0) != 0x80)
				goto invalid;

			rune cp =
				((rune) (b0 & 0x1F) << 6) |
				((rune) (b1 & 0x3F) << 0);

			if (cp < 0x80)
				goto invalid;

			return cp;
		} break;

		case 3: {
			u8 b1 = s[1];
			u8 b2 = s[2];

			if ((rune) (b1 & 0xC0) != 0x80 ||
				(rune) (b2 & 0xC0) != 0x80)
				goto invalid;

			rune cp =
				((rune) (b0 & 0x0F) << 12) |
				((rune) (b1 & 0x3F) << 6 ) |
				((rune) (b2 & 0x3F) << 0 );

			if (cp < 0x800)
				goto invalid;

			if (cp >= 0xD800 && cp <= 0xDFFF)
				goto invalid;

			return cp;
		} break;

		case 4: {
			u8 b1 = s[1];
			u8 b2 = s[2];
			u8 b3 = s[3];

			if ((b1 & 0xC0) != 0x80 ||
				(b2 & 0xC0) != 0x80 ||
				(b3 & 0xC0) != 0x80)
				goto invalid;

			rune cp =
				((rune) (b0 & 0x07) << 18) |
				((rune) (b1 & 0x3F) << 12) |
				((rune) (b2 & 0x3F) << 6 ) |
				((rune) (b3 & 0x3F) << 0 );

			if (cp < 0x10000)
				goto invalid;

			if (cp > 0x10FFFF)
				goto invalid;

			return cp;
		} break;
	}

invalid:
	if (width) *width = 1;
	return 0xFFFD;
}

funcdef string
utf8_encode(rune cp, Arena *arena)
{
	bytes out = alloc_slice(arena, u8, 4);

	if (cp > 0x10FFFF ||
	    (cp >= 0xD800 && cp <= 0xDFFF))
	{
		cp = 0xFFFD;
	}

	/* 1 byte */
	if (cp <= 0x7F)
	{
		out[0] = (u8)cp;
		return slice(string_from_bytes(out), 0, 1);
	}

	/* 2 byte */
	if (cp <= 0x7FF)
	{
		out[0] = 0xC0 | ((cp >> 6) & 0x1F);
		out[1] = 0x80 | ((cp >> 0) & 0x3F);
		return slice(string_from_bytes(out), 0, 2);
	}

	/* 3 byte */
	if (cp <= 0xFFFF)
	{
		out[0] = 0xE0 | ((cp >> 12) & 0x0F);
		out[1] = 0x80 | ((cp >> 6 ) & 0x3F);
		out[2] = 0x80 | ((cp >> 0 ) & 0x3F);
		return slice(string_from_bytes(out), 0, 3);
	}

	/* 4 byte */
	out[0] = 0xF0 | ((cp >> 18) & 0x07);
	out[1] = 0x80 | ((cp >> 12) & 0x3F);
	out[2] = 0x80 | ((cp >> 6 ) & 0x3F);
	out[3] = 0x80 | ((cp >> 0 ) & 0x3F);

	return slice(string_from_bytes(out), 0, 4);
}

funcdef u64
utf8_prev_boundary(string data, u64 i)
{
	if (i == 0) return 0;
    do { i--; } while (i > 0 && utf8_continuation_byte(data.raw[i]));
    return i;
}


funcdef string
string_format(Arena *arena, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	int len = vsnprintf(nullptr, 0, fmt, args);
	va_end(args);

	if (len < 0) return {};

	bytes buf = alloc_slice(arena, u8, len + 1);

	va_start(args, fmt);
	vsnprintf((char *)buf.raw, len + 1, fmt, args);
	va_end(args);

	return { buf.raw, (u64) len };
}

funcdef u64
string_count_lines(string s)
{
	if (s.len == 0) return 0;

	u64 count = 0;
	for (u64 i=0; i<s.len; ++i) {
		if (s[i] == '\n') count += 1;
	}
	if (s[s.len-1] != '\n') count += 1;
	return count;
}

funcdef u64
string_column_count(string s, int indent_width)
{
	u64 count = 0;

	int width = 0;

	for (u64 i = 0; i < s.len; i += width) {
		rune c = utf8_decode(
			slice(s, i, s.len),
			&width
		);

		if (c == '\n') {
			break;
		}
		else if (c == '\t') {
			u64 remainder = count % indent_width;
			count += indent_width - remainder;
		}
		else {
			count += 1;
		}
	}

	return count;
}

funcdef string
string_concat(string a, string b, Arena *arena)
{
	bytes data = alloc_slice(arena, u8, a.len + b.len);

	memcpy(data.raw, a.raw, a.len);
	memcpy(data.raw + a.len, b.raw, b.len);

	return string_from_bytes(data);
}


funcdef u64
string_find_first(string s, rune r)
{
	u64 i=0;
	while (i < s.len) {
		int width = 0;
		rune codepoint = utf8_decode(slice(s, i, s.len), &width);
		if (codepoint == r) return i;
		i += width;
	}
	return s.len;
}

funcdef u64
string_find_last(string s, rune r)
{
	s64 i = s64(s.len) - 1;

	while (i >= 0) {
		while (i >= 0 && utf8_continuation_byte(s[i])) {
			i -= 1;
		}

		if (i < 0) break;

		int width = 0;
		rune codepoint = utf8_decode(slice(s, i, s.len), &width);

		if (codepoint == r)
			return u64(i);

		i -= 1;
	}

	return s.len;
}

funcdef bool
is_space(rune r)
{
	return r == ' ' || r == '\t' || r == '\n' || r == '\r';
}

funcdef string
string_strip(string s)
{
	u64 begin = 0;
	u64 end = s.len;

	while (begin < end && is_space(s[begin])) begin += 1;
	while (end > begin && is_space(s[end - 1])) end -= 1;

	return slice(s, begin, end);
}

funcdef Slice<string>
string_split(string original, Arena *arena)
{
	u64 count = 0;
	bool in_token = false;

	for (u64 i = 0; i < original.len; ++i)
	{
		if (is_space(original[i])) {
			in_token = false;
		}
		else if (!in_token)
		{
			in_token = true;
			count += 1;
		}
	}

	Slice<string> result = alloc_slice(arena, string, count);

	u64 index = 0;
	u64 start = 0;

	in_token = false;

	for (u64 i = 0; i < original.len; ++i)
	{
		if (is_space(original[i]))
		{
			if (in_token)
			{
				result[index++] = slice(original, start, i);
				in_token = false;
			}
		}
		else if (!in_token)
		{
			start = i;
			in_token = true;
		}
	}

	if (in_token) {
		result[index++] = slice(original, start, original.len);
	}

	return result;
}

funcdef bool
string_equal(string a, string b)
{
	if (a.len != b.len) return false;
	if (a.raw == b.raw) return true;

	return memcmp(a.raw, b.raw, a.len) == 0;
}

funcdef string
string_copy(string str, Arena *arena)
{
	bytes data = alloc_slice(arena, u8, str.len);
	memcpy(data.raw, str.raw, str.len);
	return string_from_bytes(data);
}

funcdef Slice<string>
string_list(u8 **cstring, u64 len, Arena *arena)
{
	Slice<string> result = alloc_slice(arena, string, len);
	for (u64 i=0; i<len; ++i) {
		u64 l = strlen((char *) cstring[i]);
		string s = { cstring[i], l };
		result[i] = string_copy(s, arena);
	}
	return result;
}
