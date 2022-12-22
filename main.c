#include <io.h>
#include <conio.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>

#define return_defer(value) do { result = (value); goto defer; } while (0)
#define UNUSED(x) (void)(x)

#define EDITOR_CAPACITY 1024

#define ECHO ENABLE_ECHO_INPUT
#define ICANON ENABLE_LINE_INPUT

#define isatty(fd) _isatty(_fileno(fd))

typedef struct {
    size_t begin;
    size_t end;
} Line;

typedef struct {
    char data[EDITOR_CAPACITY];
    Line lines[EDITOR_CAPACITY + 10];
    size_t lines_count;
    size_t data_count;
    size_t cursor;
} Editor;

// REFERENCE: https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes (GetLastError() codes)

void editor_recompute_lines(Editor *e) {
    e->lines_count = 0;

    size_t begin = 0;
    for (size_t i = 0; i < e->data_count; ++i) {
        if (e->data[i] == '\n') {
            e->lines[e->lines_count].begin = begin;
            e->lines[e->lines_count].end = i;
            e->lines_count += 1;
            begin = i + 1;
        }
    }

    e->lines[e->lines_count].begin = begin;
    e->lines[e->lines_count].end = e->data_count;
    e->lines_count += 1;
}

void editor_insert_char(Editor *e, char x) {
    if (e->data_count < EDITOR_CAPACITY) {
        memmove(&e->data[e->cursor + 1], &e->data[e->cursor], e->data_count - e->cursor);
        e->data[e->cursor] = x;
        e->cursor += 1;
        e->data_count += 1;
        editor_recompute_lines(e);
    }
}

size_t editor_current_line(const Editor *e) {
    assert(e->cursor <= e->data_count);
    for (size_t i = 0; i < e->lines_count; ++i) {
        if (e->lines[i].begin <= e->cursor && e->cursor <= e->lines[i].end) {
            return i;
        }
    }
    return 0;
}

void editor_rerender(const Editor *e, bool insert) {
    printf("\x1b[2J\x1b[1;1H");
    fwrite(e->data, 1, e->data_count, stdout);
    printf("\n");
    if (insert) printf("[INSERT]");
    size_t line = editor_current_line(e);
    printf("\x1b[%d;%dH", (unsigned int)(line + 1), (unsigned int)(e->cursor - e->lines[line].begin + 1));
}

static Editor editor = {0};

bool editor_save_to_file(Editor *e, const char *file_path) {
    bool result = true;
    FILE *f = fopen(file_path, "wb");
    if (f == NULL) {
        fprintf(stderr, "ERROR: could not open file %s for writing: %s\n", file_path, strerror(errno));
        return_defer(false);
    }
    fwrite(e->data, 1, e->data_count, f);
    if (ferror(f)) {
        fprintf(stderr, "ERROR: could not write into file %s: %s\n", file_path, strerror(errno));
        return_defer(false);
    }
defer:
    if (f) UNUSED(fclose(f));
    return result;
}

int editor_start_interactive(Editor *e, const char *file_path) {
    int result = 0;
    bool terminal_prepared = false;
    if (!isatty(stdin)) {
        fprintf(stderr, "ERROR: Please run the editor in a terminal!\n");
        return_defer(1);
    }
    
    DWORD term_mode;
    #define term GetStdHandle(STD_INPUT_HANDLE)
    if (!GetConsoleMode(term, &term_mode)) {
        fprintf(stderr, "ERROR: could not get the state of the terminal: %ld\n", GetLastError());
        return_defer(1);
    }
    if (!SetConsoleMode(term, term_mode & ~(ECHO | ICANON))) {
        fprintf(stderr, "ERROR: could not set the state of the terminal: %ld\n", GetLastError());
        return_defer(1);
    }

    terminal_prepared = true;
    printf("\x1b[?1049h"); // Enter alternate buffer
    bool quit = false, insert = false;
    while (!quit && !feof(stdin)) {
        editor_rerender(e, insert);
        if (insert) {
            int x = _getch();
            if (x == 27) {
                insert = false;
                editor_save_to_file(e, file_path);
            } else {
                editor_insert_char(e, x);
            }
        } else {
            int x = _getch();
            switch (x) {
                case 'q': {
                    quit = true;
                } break;
                case 'e': {
                    insert = true;
                } break;
                case 's': {
                    size_t line = editor_current_line(e);
                    size_t column = e->cursor - e->lines[line].begin;
                    if (line < e->lines_count - 1) {
                        e->cursor = e->lines[line + 1].begin + column;
                        if (e->cursor > e->lines[line + 1].end) {
                            e->cursor = e->lines[line + 1].end;
                        }
                    }
                } break;
                case 'w': {
                    size_t line = editor_current_line(e);
                    size_t column = e->cursor - e->lines[line].begin;
                    if (line > 0) {
                        e->cursor = e->lines[line - 1].begin + column;
                        if (e->cursor > e->lines[line - 1].end) {
                            e->cursor = e->lines[line - 1].end;
                        }
                    }
                } break;
                case 'a': {
                    if (e->cursor > 0) e->cursor -= 1;
                } break;
                case 'd': {
                    if (e->cursor < e->data_count) e->cursor += 1;
                } break;
            }
        }
    }

defer:
    if (terminal_prepared) {
        printf("\x1b[2J\x1b[?1049l");
        SetConsoleMode(term, term_mode); // Restore console state.
    }
    return result;
}

int main(int argc, char *argv[]) {
    int result = 0;
    FILE *f = NULL;
    if (argc < 2) {
        fprintf(stderr, "Usage: need <input.txt>\n");
        fprintf(stderr, "ERROR: no input file is provided\n");
        return_defer(1);
    }
    
    const char* file_path = argv[1];
    f = fopen(file_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "ERROR: could not open file %s: %s\n", file_path, strerror(errno));
        return_defer(1);
    }
    editor.data_count = fread(editor.data, 1, EDITOR_CAPACITY, f);
    fclose(f);
    f = NULL;

    editor_recompute_lines(&editor);
    int exit_code = editor_start_interactive(&editor, file_path);
    return_defer(exit_code);
defer:
    if (f) fclose(f);
    return result;
}
