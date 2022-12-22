#include <io.h>
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <windows.h>
#include <errhandlingapi.h>

#ifdef __MINGW32__
    #define printf __mingw_printf
#endif

#define EDITOR_CAPACITY 1024

#define ANSI_ESC "\x1b"
#define ANSI_CSI "\x1b["
#define ANSI_HOME ANSI_CSI"H"
#define ANSI_CLRSCR ANSI_CSI"2J"
#define ANSI_ALTBUF ANSI_CSI"?1049h"
#define ANSI_MAINBUF ANSI_CSI"?1049l"

#define KEY_ESC 27

typedef struct {
    size_t begin;
    size_t end;
} Line;

typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
} EditorMode;

typedef struct {
    char data[EDITOR_CAPACITY];
    Line lines[EDITOR_CAPACITY + 16];
    size_t data_count;
    size_t line_count;
    size_t cursor;
    EditorMode mode;
} Editor;

Editor editor = {0};

int term_init(DWORD *mode, HANDLE *console) {
    GetConsoleMode(*console, mode);
    return (int)SetConsoleMode(*console, (*mode) & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT));
}

int term_restore(DWORD *mode, HANDLE *console) {
    return (int)SetConsoleMode(*console, *mode);
}

size_t editor_current_line(const Editor *editor) {
    assert(editor->cursor <= editor->data_count);
    for (size_t i = 0; i < editor->line_count; ++i) {
        if (editor->lines[i].begin <= editor->cursor && editor->cursor <= editor->lines[i].end) return i;
    }
    return 0;
}

void editor_render(Editor *editor) {
    printf(ANSI_CLRSCR);
    printf(ANSI_HOME);
    fwrite(editor->data, sizeof(char), editor->data_count, stdout);
    printf("\n");
    if (editor->mode == MODE_INSERT) printf("[INSERT]");
    size_t line = editor_current_line(editor);
    printf("%s%zu;%zuH", ANSI_CSI, line + 1, editor->cursor - editor->lines[line].begin + 1 );
}


void editor_compute_lines(Editor *editor) {
    editor->line_count = 0;
    size_t begin = 0;
    for (size_t i = 0; i < editor->data_count; ++i) {
        if (editor->data[i] == '\n') {
            editor->lines[editor->line_count].begin = begin;
            editor->lines[editor->line_count].end = i;
            editor->line_count += 1;
            begin = i + 1;
        }
    }
    editor->lines[editor->line_count].begin = begin;
    editor->lines[editor->line_count].end = editor->data_count;
    editor->line_count += 1;
}
void editor_insert_char(Editor *editor, char x) {
    if (editor->data_count < EDITOR_CAPACITY) {
        memmove(&editor->data[editor->cursor + 1], &editor->data[editor->cursor], editor->data_count - editor->cursor);
        editor->data[editor->cursor] = x;
        editor->cursor += 1;
        editor->data_count += 1;
        editor_compute_lines(editor);
    }
}

int main(int argc, char *argv[]) {

    if (!_isatty(fileno(stdin))) {
        fprintf(stderr, "ERROR: Please run in a terminal.\n");
        return 1;
    }
    if (argc < 2) {
        fprintf(stderr, "ERROR: Please provide an input file.\n");
        return 1;
    }

    editor_compute_lines(&editor);
    HANDLE console = GetStdHandle(STD_INPUT_HANDLE);
    DWORD console_mode = 0;

    if (!term_init(&console_mode, &console)) {
        fprintf(stderr, "ERROR: Could not set console mode. CODe: %lu\n", GetLastError());
        return 1;
    }

    char *file_path = argv[1];
    FILE *file = fopen(file_path, "rb");
    editor.data_count = fread(editor.data, sizeof(char), EDITOR_CAPACITY, file);
    if (file == NULL) {
        fprintf(stderr, "ERROR: Could not open file at %s.\n", file_path);
        return 1;
    }
    fclose(file);

    bool quit = false;
    printf(ANSI_ALTBUF);
    editor_render(&editor);
    printf(ANSI_HOME);
    editor_compute_lines(&editor);
    while (!quit && !feof(stdin)) {
        editor_render(&editor);
        int x = _getch(); // Must use _getch; literally every other function is not reading the ESC character.
        switch (editor.mode) {
            case MODE_NORMAL: {
                switch (x) {
                    case 'q': {
                        quit = true;
                    } break;
                    case 'e': {
                        editor.mode = MODE_INSERT;
                    } break;
                    // Movement
                    case 's': {
                        size_t line = editor_current_line(&editor);
                        size_t column = editor.cursor - editor.lines[line].begin;
                        if (line < editor.line_count - 1) {
                            editor.cursor = editor.lines[line + 1].begin + column;
                            if (editor.cursor > editor.lines[line + 1].end) {
                                editor.cursor = editor.lines[line + 1].end;
                            }
                        }
                    } break;
                    case 'w': {
                        size_t line = editor_current_line(&editor);
                        size_t column = editor.cursor - editor.lines[line].begin;
                        if (line > 0) {
                            editor.cursor = editor.lines[line - 1].begin + column;
                            if (editor.cursor > editor.lines[line - 1].end) {
                                editor.cursor = editor.lines[line - 1].end;
                            }
                        }
                    } break;
                    case 'a': {
                        if (editor.cursor > 0) editor.cursor -= 1;
                    } break;
                    case 'd': {
                        if (editor.cursor < editor.data_count) editor.cursor += 1;
                    } break;
                }
            } break;
            case MODE_INSERT: {
                if (x == KEY_ESC) {
                    editor.mode = MODE_NORMAL;
                    FILE *file = fopen(file_path, "wb");
                    assert(file != NULL && "TO DO: properly handle inability to autosave files");
                    fwrite(editor.data, sizeof(char), editor.data_count, file);
                    assert(!ferror(file) && "TO DO: properly handle inability to autosave files");
                    fclose(file);
                } else {
                    editor_insert_char(&editor, x);
                }
            } break;
        }
    }
    printf("%s %s", ANSI_CLRSCR, ANSI_MAINBUF);

    if (!term_restore(&console_mode, &console)) {
        fprintf(stderr, "ERROR: Could not reset console mode, CODE: %lu\n", GetLastError());
        return 1;
    }
    return 0;
}
