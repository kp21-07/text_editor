#include "../base.cpp"
#include "../os.cpp"
#include "../arena.cpp"
#include "../string.cpp"
#include "../gfx.cpp"
#include "../ui.cpp"
#include "../buffer.cpp"
#include "../editor.cpp"
#include "../main.cpp"

#include "../languages/c_plus_plus.cpp"
#include "../languages/editor_script.cpp"

#include "../vendor/glad.c"

#if OS_Windows

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int show_cmd)
{
	(void) instance;
	(void) prev_instance;
	(void) cmd_line;
	(void) show_cmd;

	int argc = 0;
	LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
	slice<string> args = strings_from_cstrings(scratch(), argc, (char **) wargv);

	LocalFree(wargv);

	entry_point(args);
	return 0;
}

#else

int main(int argc, char **argv) {
	slice<string> args = strings_from_cstrings(scratch(0, 0), argc, argv);
	entry_point(args);

	return 0;
}

#endif
