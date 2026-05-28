#include "platform.cpp"
#include "alloc.cpp"
#include "string.cpp"
#include "buffer.cpp"
#include "editor.cpp"
#include "graphics.cpp"
#include "ui.cpp"
#include "main.cpp"

int main(int argc, char **argv) {
	Arena *arg_arena = arena_new(KB(4));
	Slice<string> args = string_list((u8 **) argv, (u64) argc, arg_arena);

	entry_point(args);
}

