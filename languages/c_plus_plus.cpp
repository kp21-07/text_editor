#include "../editor.h"

funcdef bool
is_cpp_operator(string s)
{
	switch (s.len) {
	case 1:
		switch (s[0]) {
		case '+': case '-': case '*': case '/':
		case '%': case '=': case '<': case '>':
		case '!': case '&': case '|': case '^':
		case '~': case '?': case ':':
		case '.': case ',': case ';':
		case '(': case ')':
		case '[': case ']':
		case '{': case '}':
			return true;
		}
		break;

	case 2:
		return
			string_equal(s, S("==")) ||
			string_equal(s, S("!=")) ||
			string_equal(s, S("<=")) ||
			string_equal(s, S(">=")) ||
			string_equal(s, S("++")) ||
			string_equal(s, S("--")) ||
			string_equal(s, S("->")) ||
			string_equal(s, S("&&")) ||
			string_equal(s, S("||")) ||
			string_equal(s, S("+=")) ||
			string_equal(s, S("-=")) ||
			string_equal(s, S("*=")) ||
			string_equal(s, S("/=")) ||
			string_equal(s, S("%=")) ||
			string_equal(s, S("&=")) ||
			string_equal(s, S("|=")) ||
			string_equal(s, S("^=")) ||
			string_equal(s, S("<<")) ||
			string_equal(s, S(">>")) ||
			string_equal(s, S("::"));

	case 3:
		return
			string_equal(s, S("<<=")) ||
			string_equal(s, S(">>=")) ||
			string_equal(s, S("..."));
	}

	return false;
}

funcdef bool
is_cpp_type(string s)
{
	if (s.len == 0)
		return false;

	if (s.len >= 3) {
		string last_two = s.range(s.len - 2, s.len);
		if (string_equal(last_two, S("_t")))
			return true;
	}

	switch (s.len) {
	case 2:
		return
			string_equal(s, S("s8")) || 
			string_equal(s, S("u8"));

	case 3:
		return
			string_equal(s, S("int")) || 
			string_equal(s, S("s64")) || 
			string_equal(s, S("u64")) || 
			string_equal(s, S("s32")) || 
			string_equal(s, S("u32")) || 
			string_equal(s, S("s16")) || 
			string_equal(s, S("u16")) || 
			string_equal(s, S("f64")) || 
			string_equal(s, S("f32"));

	case 4:
		return
			string_equal(s, S("void")) ||
			string_equal(s, S("bool")) ||
			string_equal(s, S("long")) ||
			string_equal(s, S("char"));

	case 5:
		return
			string_equal(s, S("short")) ||
			string_equal(s, S("float"));

	case 6:
		return
			string_equal(s, S("double")) ||
			string_equal(s, S("signed")) ||
			string_equal(s, S("string"));
	default:
		return false;
	}
}

funcdef bool
is_cpp_keyword(string s)
{
	if (s.len == 0)
		return false;

	switch (s.len) {
	case 2:
	switch (s[0]) {
		case 'd': return string_equal(s, S("do"));
		case 'i': return string_equal(s, S("if"));
	} break;

	case 3:
	switch (s[0]) {
		case 'a': return string_equal(s, S("asm"));
		case 'f': return string_equal(s, S("for"));
		case 'n': return string_equal(s, S("new"));
		case 't': return string_equal(s, S("try"));
	} break;

	case 4:
	switch (s[0]) {
		case 'a': return string_equal(s, S("auto"));
		case 'c': return string_equal(s, S("case"));
		case 'e': return string_equal(s, S("else")) ||
		                 string_equal(s, S("enum"));
		case 'g': return string_equal(s, S("goto"));
		case 'l': return string_equal(s, S("long"));
		case 't': return string_equal(s, S("this")) ||
		                 string_equal(s, S("true"));
	} break;

	case 5:
	switch (s[0]) {
		case 'b': return string_equal(s, S("break"));
		case 'c': return string_equal(s, S("catch")) ||
		                 string_equal(s, S("class")) ||
		                 string_equal(s, S("const"));
		case 'f': return string_equal(s, S("false"));
		case 's': return string_equal(s, S("short"));
		case 't': return string_equal(s, S("throw"));
		case 'u': return string_equal(s, S("union")) ||
		                 string_equal(s, S("using"));
		case 'w': return string_equal(s, S("while"));
	} break;

	case 6:
	switch (s[0]) {
		case 'd': return string_equal(s, S("delete"));
		case 'e': return string_equal(s, S("export")) ||
		                 string_equal(s, S("extern"));
		case 'f': return string_equal(s, S("friend"));
		case 'i': return string_equal(s, S("inline"));
		case 'p': return string_equal(s, S("public"));
		case 'r': return string_equal(s, S("return"));
		case 's': return string_equal(s, S("signed")) ||
		                 string_equal(s, S("sizeof")) ||
		                 string_equal(s, S("static")) ||
		                 string_equal(s, S("struct")) ||
		                 string_equal(s, S("switch"));
		case 't': return string_equal(s, S("typeid"));
	} break;

	case 7:
	switch (s[0]) {
		case 'a': return string_equal(s, S("alignas"));
		case 'd': return string_equal(s, S("default"));
		case 'm': return string_equal(s, S("mutable"));
		case 'p': return string_equal(s, S("private"));
		case 't': return string_equal(s, S("typedef"));
		case 'v': return string_equal(s, S("virtual"));
	} break;

	case 8:
	switch (s[0]) {
		case 'a': return string_equal(s, S("alignof"));
		case 'c': return string_equal(s, S("continue"));
		case 'd': return string_equal(s, S("decltype"));
		case 'e': return string_equal(s, S("explicit"));
		case 'n': return string_equal(s, S("noexcept"));
		case 'o': return string_equal(s, S("operator"));
		case 'r': return string_equal(s, S("register"));
		case 't': return string_equal(s, S("template")) ||
		                 string_equal(s, S("typename"));
		case 'u': return string_equal(s, S("unsigned"));
		case 'v': return string_equal(s, S("volatile"));
	} break;

	case 9:
	switch (s[0]) {
		case 'c': return string_equal(s, S("constexpr"));
		case 'n': return string_equal(s, S("namespace"));
		case 'p': return string_equal(s, S("protected"));
	} break;

	case 10: return string_equal(s, S("const_cast"));
	case 11: return string_equal(s, S("static_cast"));
	case 12: return string_equal(s, S("dynamic_cast")) ||
			        string_equal(s, S("thread_local"));
	case 13: return string_equal(s, S("static_assert"));
	case 16: return string_equal(s, S("reinterpret_cast"));
	}

	return false;
}

funcdef void
tokenize_source_code_cpp(Lexer_State *state, string source, Token_List *tokens, u64 scan_start, u64 scan_end)
{
	u64 i = scan_start;
	u64 source_len = scan_end;

	if (*state == Lex_State_InComment) {
		u32 start = scan_start;

		while (i < source_len) {
			if (i + 1 < source_len &&
			    source[i] == '*' &&
			    source[i + 1] == '/')
			{
				i += 2;
				*state = Lex_State_None;
				break;
			}

			i += 1;
		}

		Lang_Token token = {
			start,
			(u16) (i - start),
			Token_Comment,
		};
		big_array_push(tokens, token);
	}

	while (i < source_len) {
		char c = source[i];

		if (is_space(c)) {
			i += 1;
			continue;
		}

		//
		// preprocessor / macros
		//

		if (c == '#') {
			u32 start = i;
			i += 1;

			while (i < source_len && source[i] == ' ') {
				i += 1;
			}

			while (i < source_len && char_kind(source[i]) == Char_Letter) {
				i += 1;
			}

			string directive_text = source.range(start, i);
			bool is_include = false;

			for (u32 j = start; j < i; j++) {
				if (source[j] == 'i') {
					string candidate = source.range(j, Min(source.len, j + 7));
					if (string_equal(candidate, S("include"))) {
						is_include = true;
						break;
					}
				}
			}

			Lang_Token macro_token = {
				start,
				(u16)(i - start),
				Token_Macro,
			};
			big_array_push(tokens, macro_token);

			while (i < source_len && source[i] == ' ') {
				i += 1;
			}

			// handle #include <foo.h> — lex <...> as a string token
			if (is_include && i < source_len && source[i] == '<') {
				u32 str_start = i;
				i += 1;

				while (i < source_len && source[i] != '>' && source[i] != '\n') {
					i += 1;
				}

				if (i < source_len && source[i] == '>') {
					i += 1;
				}

				Lang_Token str_token = {
					str_start,
					(u16)(i - str_start),
					Token_String,
				};
				big_array_push(tokens, str_token);
			}

			continue;
		}

		//
		// comments
		//
		if (c == '/' && i + 1 < source_len) {
			if (source[i + 1] == '/') {
				u32 start = i;
				i += 2;

				while (i < source_len && source[i] != '\n' && source[i] != '\r') {
					i += 1;
				}

				Lang_Token token = {
					start,
					(u16) (i - start),
					Token_Comment,
				};
				big_array_push(tokens, token);
				continue;
			}

			if (source[i + 1] == '*') {
				u32 start = i;

				i += 2;
				*state = Lex_State_InComment;

				while (i < source_len) {
					if (i + 1 < source_len && source[i] == '*' && source[i + 1] == '/') {
						i += 2;
						*state = Lex_State_None;
						break;
					}

					i += 1;
				}

				Lang_Token token = {
					start,
					(u16) (i - start),
					Token_Comment,
				};
				big_array_push(tokens, token);
				continue;
			}
		}

		//
		// strings
		//

		if (c == '"' || c == '\'') {
			u32 start = i;
			char quote = c;

			i += 1;

			while (i < source_len) {

				if (source[i] == '\\') {
					i += 2;
					continue;
				}

				if (source[i] == quote) {
					i += 1;
					break;
				}

				i += 1;
			}

			Lang_Token token = {
				start,
				(u16) (i - start),
				Token_String,
			};
			big_array_push(tokens, token);
			continue;
		}

		//
		// identifiers / keywords
		//

		if (char_kind(c) == Char_Letter) {
			u32 start = i;

			while (i < source_len) {
				CharKind kind = char_kind(source[i]);

				if (kind != Char_Letter &&
				    kind != Char_Number)
				{
					break;
				}

				i += 1;
			}

			string text = source.range(start, i);

			Lang_TokeKind kind = 
				is_cpp_keyword(text) ? Token_Keyword : (is_cpp_type(text) ? 
						Token_Type : Token_Identifier);

			Lang_Token token = {
				start,
				(u16) (i - start),
				kind,
			};
			big_array_push(tokens, token);
			continue;
		}

		//
		// numbers
		//

		if (char_kind(c) == Char_Number) {
			u32 start = i;
			bool is_hex = false;

			if (c == '0' &&
			    i + 1 < source_len &&
			    (source[i + 1] == 'x' ||
			     source[i + 1] == 'X'))
			{
				is_hex = true;
				i += 2;
			}

			while (i < source_len) {

				char nc = source[i];

				if (is_hex) {
					bool ok = false;
					hex_char_to_int(nc, &ok);

					if (ok) {
						i += 1;
						continue;
					}
				}
				else {

					if (char_kind(nc) == Char_Number) {
						i += 1;
						continue;
					}

					if (nc == '.') {
						i += 1;
						continue;
					}
				}

				if (nc == 'u' || nc == 'U' ||
				    nc == 'l' || nc == 'L' ||
				    nc == 'f' || nc == 'F')
				{
					i += 1;
					continue;
				}

				break;
			}

			Lang_Token token = {
				start,
				(u16) (i - start),
				Token_Number,
			};
			big_array_push(tokens, token);
			continue;
		}

		//
		// operators
		//

		{
			u32 start = i;

			if (i + 3 <= source_len) {
				string op = source.range(i, i + 3);

				if (is_cpp_operator(op)) {
					i += 3;

					Lang_Token token = {
						start,
						3,
						Token_Symbol,
					};
					big_array_push(tokens, token);
					continue;
				}
			}

			if (i + 2 <= source_len) {
				string op = source.range(i, i + 2);

				if (is_cpp_operator(op)) {
					i += 2;

					Lang_Token token = {
						start,
						2,
						Token_Symbol,
					};
					big_array_push(tokens, token);
					continue;
				}
			}

			string op = source.range(i, i + 1);

			if (is_cpp_operator(op)) {
				i += 1;

				Lang_Token token = {
					start,
					1,
					Token_Symbol,
				};
				big_array_push(tokens, token);
				continue;
			}
		}

		i += 1;
	}
}
