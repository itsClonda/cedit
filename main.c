#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// METHODS AND STRUCTURES FOR RAW TERMINAL MODE

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

static HANDLE hIn, hOut;
static DWORD origInMode, origOutMode;
static size_t padding_x = 5;

static void disable_raw_mode(void) {
    printf("\x1b[?25h\x1b[?1049l");   // show cursor, leave alt screen
    fflush(stdout);
    SetConsoleMode(hIn, origInMode);
    SetConsoleMode(hOut, origOutMode);
}

static BOOL WINAPI on_ctrl_event(DWORD type) {
    disable_raw_mode();               // runs on close/logoff/shutdown events
    return FALSE;                     // let the default handler terminate us
}

static int enable_raw_mode(void) {
    hIn  = GetStdHandle(STD_INPUT_HANDLE);
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!GetConsoleMode(hIn, &origInMode) || !GetConsoleMode(hOut, &origOutMode)) {
        return -1;                    // not a console (redirected/piped)
    }

    DWORD in = origInMode;
    in &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    in |= ENABLE_VIRTUAL_TERMINAL_INPUT;   // arrow keys etc. arrive as ANSI sequences
    if (!SetConsoleMode(hIn, in)) return -1;

    DWORD out = origOutMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, out)) return -1;

    SetConsoleCtrlHandler(on_ctrl_event, TRUE);
    atexit(disable_raw_mode);
    return 0;
}

// METHODS AND STRUCTURES FOR FILE HANDLING AND DISPLAYING;

// Create structures for the line, the screen and the file.
typedef struct {
    size_t dimension_y;
    size_t dimension_x;
} Screen;
typedef struct {
    size_t capacity;
    size_t length;
} Line;
typedef struct {
    size_t capacity;
    size_t num_lines;
} File;

Screen* screen = NULL;
size_t cursor_x = 1;
size_t cursor_y = 1;
signed int changes = 0;

// Method to get a line from a file stream
static int portable_getline(char **lineptr, size_t *n, FILE *stream) {
    if (lineptr == NULL || n == NULL || stream == NULL) {
        return -1;
    }
    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = (char *)malloc(*n);
        if (*lineptr == NULL) {
            return -1;
        }
    }
    size_t index = 0;
    char c;
    while(c = fgetc(stream)) {
        if (c == EOF) {
            return -1; 
        }
        if (c == '\n') {
            (*lineptr)[index] = '\0';
            return *n - 1;
        }
        (*lineptr)[index] = c;
        if(index > *n){
            (*n)++; 
            *lineptr = (char *)realloc(*lineptr, *n);
            if (*lineptr == NULL) {
                return -1; 
            }
        }
        index++;
    }
}

// initializes the line array
Line *init_line(const char *data) {
    size_t data_length = strlen(data);
    Line *line = (Line *)malloc(sizeof(Line) + data_length + 1);
    if (line == NULL) {
        perror("Error allocating memory for line");
        exit(EXIT_FAILURE);
    }
    line->length = data_length;
    line->capacity = data_length + 1;
    char *linedata = (char *)(line + 1);
    if (linedata == NULL) {
        perror("Error allocating memory for line data");
        free(line);
        exit(EXIT_FAILURE);
    }
    strcpy(linedata, data);
    return line;
}

// initializes the file array
File *init_file(void) {
    File *file = (File *)malloc(sizeof(File) + sizeof(Line *) * 10);
    if (file == NULL) {
        perror("Error allocating memory for file");
        exit(EXIT_FAILURE);
    }
    file->capacity = 10;
    file->num_lines = 0;
    return file;
}
//Pushes a line into the file array
File *push_line(File *file, Line *line) {
    if (file->num_lines >= file->capacity) {
        size_t new_capacity = file->capacity == 0 ? 10 : file->capacity * 2;
        File *new_file = (File *)realloc(file, sizeof(File) + sizeof(Line *) * new_capacity);
        if (new_file == NULL) {
            perror("Error reallocating memory for file");
            exit(EXIT_FAILURE);
        }
        file = new_file;
        file->capacity = new_capacity;
    }

    Line **lines = (Line **)(file + 1);
    lines[file->num_lines++] = line;
    return file;
}
//Creates a File buffer struct from a filestream
File *format_file_to_buffer(FILE *file) {
    File *buffer = init_file();
    char *line = NULL;
    size_t len = 0;
    int read;

    while ((read = portable_getline(&line, &len, file)) != -1) {
        while (read > 0 && (line[read - 1] == '\n' || line[read - 1] == '\r')) {
            line[--read] = '\0';
        }

        Line *new_line = init_line(line);
        buffer = push_line(buffer, new_line);
    }

    free(line);
    return buffer;
}
// adds 1 char
size_t insert_char(File *buf, size_t row, size_t col, char c) {
    Line **slot = &((Line **)(buf + 1))[row];
    Line *line = *slot;
 
    if (col > line->length) col = line->length;
 
    if (line->length + 2 >= line->capacity) {
        size_t new_cap = line->capacity * 2;
        Line *tmp = (Line *)realloc(line, sizeof(Line) + new_cap);
        if (tmp == NULL) {
            perror("Error reallocating memory for line");
            exit(EXIT_FAILURE);
        }
        line = tmp;
        line->capacity = new_cap;
        *slot = line;
    }
 
    char *s = (char *)(line + 1);
    memmove(s + col + 1, s + col, line->length - col + 1);
    s[col] = c;
    line->length++;
    return col;
}
// deletes 1 char
void delete_char(File *buf, size_t row, size_t col) {
    if(row < 0 || col < 0){
        return;
    }
    Line *line = ((Line **)(buf + 1))[row];
    if (col >= line->length) return;

    char *s = (char *)(line + 1);
    memmove(s + col, s + col + 1, line->length - col);
    line->length--;
}
//gets a specific line from a File buffer
Line *get_line_from_buffer(File* file_buffer,size_t index){
    if(file_buffer->num_lines < index + 1){
        perror("cant access line out of file bounds");
        exit(EXIT_FAILURE);
    }
    Line** lines = (Line**)(file_buffer + 1);
    if(lines == NULL){
        perror("unable to find the lines from the buffer");
        exit(EXIT_FAILURE);
    }
    if(lines[index] == NULL){
        perror("unable to find line at specified index");
        exit(EXIT_FAILURE);
    }
    return lines[index];
}
//adds a new line
File *add_new_line(File *file_buffer, size_t index) {
    if (file_buffer == NULL) {
        fprintf(stderr, "file buffer can't be NULL\n");
        exit(EXIT_FAILURE);
    }
    if (index > file_buffer->num_lines) {
        fprintf(stderr, "cannot create a line there\n");
        exit(EXIT_FAILURE);
    }

    if (file_buffer->num_lines >= file_buffer->capacity) {
        size_t new_cap = file_buffer->capacity * 2;
        File *tmp = (File *)realloc(file_buffer, sizeof(File) + sizeof(Line *) * new_cap);
        if (tmp == NULL) {
            perror("Error reallocating memory for file");
            exit(EXIT_FAILURE);
        }
        file_buffer = tmp;
        file_buffer->capacity = new_cap;
    }

    Line **lines = (Line **)(file_buffer + 1);
    memmove(lines + index + 1, lines + index, (file_buffer->num_lines - index) * sizeof(Line*));
    lines[index] = init_line(" ");
    file_buffer->num_lines++;                      
    return file_buffer;
}

//converts from a Line* to a string
char *line_to_string(Line *line) {
    if (line == NULL) {
        fprintf(stderr, "cannot convert NULL to string\n");
        exit(EXIT_FAILURE);
    }
    return (char *)(line + 1);
}

//rerenders 1 line 
void re_render_line(size_t index, File* file_buffer){
    Line* line = get_line_from_buffer(file_buffer,index);
    printf("\x1b[%zu;%zuH\x1b[K",index + 1,padding_x);
    printf(line_to_string(line));
    printf("\n");
}

//prints the file
void display_file(File* file_buffer){
    // clear screen
    printf("\x1b[2J\x1b[H");
    for(int i = 0; i < screen->dimension_y; i++){
        if(i < file_buffer->num_lines){
            Line* line = get_line_from_buffer(file_buffer,i);
            printf("\x1b[K\x1b[90m%zu ~ \x1b[0m",i + 1);
            printf("%s",line_to_string(line));
            printf("\n");
        } else {
            printf("\x1b[K\x1b[90m # \n\x1b[0m");
        }
    }
}

//saves the file and displays the saved message
void save_file(FILE* file_out, File* file_in){
    if(file_in == NULL || file_out == NULL){
        perror("save file needs two valid pointers");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < file_in->num_lines; i++) {
        fputs(line_to_string(get_line_from_buffer(file_in, i)), file_out);
        fputc('\n', file_out);
    }
    fflush(file_out);
    fclose(file_out);
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+2, 1);
    printf("\x1b[32mFile saved!\x1b[0m");
    printf("\x1b[%zu;%zuH",cursor_y,cursor_x + padding_x - 1);
    changes = 0;
}

//reads a char from the input
static int read_key(void) {
    unsigned char ch;
    DWORD got = 0;
    if (!ReadFile(hIn, &ch, 1, &got, NULL) || got == 0) return EOF;
    return ch;
}
int main(int argc, char *argv[]) {
    screen = malloc(sizeof(Screen));
    screen->dimension_x = 80;
    screen->dimension_y = 20;
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    File *file_buffer = format_file_to_buffer(file);
    fclose(file);
    //enables raw terminal mode
    if (enable_raw_mode() != 0) {
        fprintf(stderr, "raw mode failed: %lu\n", GetLastError());
        return EXIT_FAILURE;
    }
    //enter alternate screen 
    printf("\x1b[?1049h");
    // display the inital file and move the cursor to the start
    display_file(file_buffer);
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+2, 1);
    printf("Lenght change: %zu", changes);
    printf("\x1b[1;%zuH",padding_x);
    //enter a loop
    char c;
    while(c = read_key()){
        if(c == 17) break;
        if(c == 19) {
            FILE *file = fopen(argv[1], "w+");
            if (file == NULL) {
                perror("Error opening file");
                return EXIT_FAILURE;
            }
            save_file(file,file_buffer);
            continue;
        }
        if (c == 27) {
            if (getchar() != '[') continue;
            int k = getchar();
            if (k == 'D' && cursor_x > 1) cursor_x--;
            else if (k == 'A' && cursor_y > 1) cursor_y--;
            else if (k == 'C' && cursor_x) cursor_x++;
            else if (k == 'B' && cursor_y < file_buffer->num_lines) cursor_y++;
            changes--;
        }else if((c == 127 || c == 8)){
            if(cursor_x > 1){
                delete_char(file_buffer,cursor_y - 1, cursor_x - 2);
                re_render_line(cursor_y - 1,file_buffer);
                cursor_x--;
                changes--;
            }
            changes--;
        } else if(c == 13 || c == 10) {
            cursor_y++;
            cursor_x = 1;
            file_buffer = add_new_line(file_buffer,cursor_y - 1);
            display_file(file_buffer);
        } else if(c >= 32 && c < 127){
            size_t col = insert_char(file_buffer, cursor_y - 1, cursor_x - 1, c);
            re_render_line(cursor_y - 1,file_buffer);
            cursor_x = col + 2;
        }
        changes++;
        printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+2, 1);
        printf("Lenght change: %d", changes);
        printf("\x1b[%zu;%zuH",cursor_y,cursor_x + padding_x - 1);
    }
    //leave alternate screen
    free(file_buffer);
    return EXIT_SUCCESS;
}