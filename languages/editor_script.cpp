#include "../editor.h"

funcdef void
tokenize_source_code_config(Lexer_State *state, string source,
                            Token_List *tokens,
                            u64 scan_start, u64 scan_end)
{
    (void) state;

    u64 i   = scan_start;
    u64 end = scan_end;

    while (i < end) {
        char c = source[i];

        if (is_space(c)) {
            i += 1;
            continue;
        }

        u32 start = (u32)i;

        if (c == '#') {
            i += 1;

            while (i < end &&
                   source[i] != '\n' &&
                   source[i] != '\r')
            {
                i += 1;
            }

            big_array_push(tokens, {
                start,
                (u16)(i - start),
                Token_Comment,
            });

            continue;
        }

        // Everything else is an identifier.
        while (i < end &&
               !is_space(source[i]) &&
               source[i] != '#')
        {
            i += 1;
        }

        big_array_push(tokens, {
            start,
            (u16)(i - start),
            Token_Identifier,
        });
    }
}
