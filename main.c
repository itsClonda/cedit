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
    size_t y_index;
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
static size_t padding_x = 3;
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
    if(buffer->num_lines == 0){
        Line *new_line = init_line("");
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

//merges two lines of the file buffer
void merge_lines(File* file_buffer, size_t row) {
    if(row == 0){
        return;
    }
    Line** lines = (Line**) (file_buffer + 1);
    Line* line_1 = lines[row - 1];
    size_t cursor_x_position = line_1->length;
    Line* line_2 = lines[row]; 
    line_1->capacity = strlen((char*) (line_1 + 1)) + strlen((char*) (line_2 + 1)) + 1;
    line_1->length = line_1->capacity - 1;
    Line* tmp = realloc(line_1, sizeof(Line) + line_1->capacity);
    if(tmp == NULL){
        perror("unable to merge lines");
        exit(EXIT_FAILURE);
    }
    lines[row - 1] = tmp;
    strcat((char*) (tmp + 1),(char*) (line_2 + 1));
    memmove(lines + row, lines + row + 1,(file_buffer->num_lines - row - 1)*sizeof(Line*));
    free(line_2);
    file_buffer->num_lines--;
    cursor_y--;
    cursor_x = cursor_x_position + 1;
};

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
File *add_new_line(File *file_buffer, size_t index, size_t col) {
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
    Line* line_to_modify = lines[index];
    if(col > line_to_modify->length){
        col = line_to_modify->length;
    }
    char* text = (char*) (line_to_modify + 1);
    char* next_line_text = malloc(line_to_modify->length - col + 1);
    line_to_modify->length = col;
    strcpy(next_line_text, text + col);
    text[col] = '\0';
    memmove(lines + index + 1, lines + index, (file_buffer->num_lines - index) * sizeof(Line*));
    lines[index] = line_to_modify;
    lines[index + 1] = init_line(next_line_text);
    file_buffer->num_lines++;    
    free(next_line_text);                  
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
void re_render_line(size_t index, File* file_buffer, size_t col) {
    Line* line = get_line_from_buffer(file_buffer,index);
    printf("\x1b[?25l\x1b[%zu;%zuH\x1b[K",index + 1 - screen->y_index, col + 1 + padding_x);
    char* line_str = line_to_string(line);
    fputs(line_str + col, stdout);
    printf("\n");
}

//prints the file
void display_file(File* file_buffer){
    // clear screen
    printf("\x1b[H\x1b[?25l");
    for(int i = screen->y_index; i < (screen->dimension_y + screen->y_index); i++){
        if(i < file_buffer->num_lines){
            Line* line = get_line_from_buffer(file_buffer,i);
            printf("\x1b[K\x1b[90m ~ \x1b[0m");
            fputs(line_to_string(line), stdout);
            printf("\n");
        } else {
            printf("\x1b[K\x1b[90m # \n\x1b[0m");
        }
    }
}

// FUNCTIONS FOR COMMAND PALETTE

//reads a char from the input (used also in the main loop)
static int read_key(void) {
    unsigned char ch;
    DWORD got = 0;
    if (!ReadFile(hIn, &ch, 1, &got, NULL) || got == 0) return EOF;
    return ch;
}

Line* command;
int is_command_insered = 0;
int is_command_mode = 0;
size_t command_mode_cursor_x = 1;
size_t command_mode_cursor_y = 1;
//displays command palette
void display_command_palette() {
    if(command->length > 0){
        is_command_insered = 1;
    } else {
        is_command_insered = 0;
    }
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+4, 1);
    if (is_command_insered) {
        printf("\x1b[33m%s\x1b[0m", line_to_string(command));
    } else {
        if(is_command_mode){
            printf("\x1b[33mEnter command... \x1b[0m");
        } else {
            printf("\x1b[33mEnter command mode with Ctrl+P \x1b[0m");
        }
    }
    fflush(stdout);
}
//initializes command palette
void init_command_palette() {
    command = init_line("");
    command_mode_cursor_y = screen->dimension_y + 4;
    if (command == NULL) {
        perror("Error allocating memory for command palette");
        exit(EXIT_FAILURE);
    }
    display_command_palette();
}
//frees command palette
void free_command_palette() {
    free(command);
}

//inserts a char into the command palette
size_t insert_char_command_palette(size_t col, char c) {
    if (col > command->length) col = command->length;
 
    if (command->length + 2 >= command->capacity) {
        size_t new_cap = command->capacity * 2;
        Line *tmp = (Line *)realloc(command, sizeof(Line) + new_cap);
        if (tmp == NULL) {
            perror("Error reallocating memory for line");
            exit(EXIT_FAILURE);
        }
        command = tmp;
        command->capacity = new_cap;
    }
 
    char *s = (char *)(command + 1);
    memmove(s + col + 1, s + col, command->length - col + 1);
    s[col] = c;
    command->length++;
    return col;
}

//deletes a char from the command palette
void delete_char_command_palette(size_t col) {
    if(col < 0){
        return;
    }
    if (col >= command->length) return;

    char *s = (char *)(command + 1);
    memmove(s + col, s + col + 1, command->length - col);
    command->length--;
}

//prints the output/name of a command
void print_command_name(char* output){
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+6, 1);
    printf("\x1b[90m-> %s\x1b[0m", output);
}
void print_command_output(char* output){
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+9, 1);
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+8, 1);
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+7, 1);
    printf("%s",output);
}

//evaluates a command and calls the right function to process it
void evaluate_command(File* file_buffer){
    char* text = (char*) (command + 1);
    if(text[0] != '$') {
        print_command_name("\x1b[31m A valid command starts with a $");
    } else {
        //prints command
        print_command_name(text);
        //evaluates the commands
        if(strstr(text,"lns") || strstr(text,"lines")){
            char mess_string[128];
            snprintf(mess_string,sizeof(mess_string),"The program has %zu lines.",file_buffer->num_lines);
            print_command_output(mess_string);
        } else if(strstr(text,"jmp") || strstr(text,"jump")) {
            print_command_output("Enter line to jump to: ");
            int c;
            int digit = 1;
            int final_number = 0;
            while(c = read_key()){
                if(c == 13 || c == 10) break;
                if(c >= '0' && c <= '9') {
                    int number = c - '0';
                    if(digit > 1) final_number *= 10;
                    final_number += number;
                    digit++;
                    printf("%c",c);
                }
            }
            if(final_number < 1){
                print_command_output("Invalid line number");
                return;
            }
            char mess_string[128];
            snprintf(mess_string,sizeof(mess_string),(final_number <= file_buffer->num_lines) ? "Jumping to line: %zu" : "Line is out of this file",final_number);
            print_command_output(mess_string);
            if(final_number <= file_buffer->num_lines) {
                size_t computed_y_index = (final_number / screen->dimension_y)*(screen->dimension_y) -1*(final_number / screen->dimension_y > 0);
                cursor_y = final_number;
                screen->y_index = computed_y_index;
                display_file(file_buffer);
            }
        } else if(strstr(text,"src") || strstr(text,"search")) {
            print_command_output("Enter keyword to find (max 23 ch): ");
            char keyword[24];
            int c;
            int n_of_chars = 0;
            while(c = read_key()){
                if(c == 13 || c == 10 || n_of_chars >= 22) break;
                if(c >= 32 && c <= 126){
                    keyword[n_of_chars] = c;
                    n_of_chars++;
                    printf("%c",c);
                }
            }
            keyword[n_of_chars] = '\0';
            if(keyword[0] == '\0'){
                print_command_output("Passed in an empty string");
                return;
            }
            print_command_output("Keyword found at lines: ");
            Line** lines = (Line**) (file_buffer + 1);
            for(size_t i = 0; i < file_buffer->num_lines; i++){
                Line* line = lines[i];
                if(strstr((char*)(line + 1),keyword)){
                    printf("%zu, ",i + 1);
                }
            }
        } else if(strstr(text,"cll") || strstr(text,"clearline")) {
            Line** lines = (Line**) (file_buffer + 1);
            size_t line_length = lines[cursor_y - 1]->length;
            changes -= line_length;
            free(lines[cursor_y - 1]);
            lines[cursor_y - 1] = init_line("");
            display_file(file_buffer);
            print_command_output("Cleared the line");
        } else if(strstr(text,"swp") || strstr(text,"swap")) {
            print_command_output("Enter line to swap with: ");
            int c;
            int digit = 1;
            int final_number = 0;
            while(c = read_key()){
                if(c == 13 || c == 10) break;
                if(c >= '0' && c <= '9') {
                    int number = c - '0';
                    if(digit > 1) final_number *= 10;
                    final_number += number;
                    digit++;
                    printf("%c",c);
                }
            }
            if(final_number < 1){
                print_command_output("Invalid line number");
                return;
            }
            char mess_string[128];
            snprintf(mess_string,sizeof(mess_string),(final_number <= file_buffer->num_lines) ? "Swapping with line: %zu" : "Line is out of this file",final_number);
            print_command_output(mess_string);
            if(final_number <= file_buffer->num_lines) {
                Line* tmp = get_line_from_buffer(file_buffer,final_number - 1);
                Line** lines = (Line**) (file_buffer + 1);
                lines[final_number - 1] = lines[cursor_y - 1];
                lines[cursor_y - 1] = tmp;
                display_file(file_buffer);
            }
        }else {
            print_command_output("\x1b[31m Unknown command");
        }
    } 
        
    command_mode_cursor_x = 1;
    free(command);
    command = init_line("");
}

// VITAL FUNCTIONS
//saves the file and displays the saved message
void save_file(FILE* file_out, File* file_in){
    if(file_in == NULL || file_out == NULL){
        perror("save file needs two valid pointers");
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < file_in->num_lines; i++) {
        fputs(line_to_string(get_line_from_buffer(file_in, i)), file_out);
        if (i < file_in->num_lines) fputc('\n', file_out);
    }
    fflush(file_out);
    fclose(file_out);
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+3, 1);
    printf("\x1b[32mFile saved!\x1b[0m");
    printf("\x1b[%zu;%zuH",(is_command_mode == 1) ? (command_mode_cursor_y) : (cursor_y - screen->y_index),(is_command_mode == 1) ? command_mode_cursor_x : (cursor_x + padding_x));
    changes = 0;
}

int main(int argc, char *argv[]) {
    screen = malloc(sizeof(Screen));
    screen->dimension_x = 80;
    screen->dimension_y = 20;
    screen->y_index = 0;
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *file = fopen(argv[1], "a+");
    if (file == NULL) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }
    rewind(file);
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
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+3, 1);
    printf("Length change: %d - \x1b[90mUnsaved\x1b[0m", changes);
    printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+1,1);
    for(size_t i = 0; i < screen->dimension_x; i++) {
        printf("-");
    }
    //inits the command palette
    init_command_palette();
    //move the cursor to the start
    printf("\x1b[1;%zuH",padding_x + 1);
    //enter a loop
    char c;
    signed int prev_changes = 0;
    while(c = read_key()){
        //handles specific ctrl key commands (quit, save, command mode)
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
        if(c == 16){
            is_command_mode = (is_command_mode == 1) ? 0 : 1;
            display_command_palette();
        }
        //handles typing mode
        if(is_command_mode == 0){
            prev_changes = changes;
            if (c == 27) {
                if (getchar() != '[') continue;
                int k = getchar();
                if (k == 'D' && cursor_x > 1) cursor_x--;
                else if (k == 'A' && cursor_y > 1) {
                    cursor_y--;
                    if(cursor_y < screen->y_index + 1 && screen->y_index > 0){
                        screen->y_index--;
                        display_file(file_buffer);
                    }
                }
                else if (k == 'C' && cursor_x) cursor_x++;
                else if (k == 'B' && cursor_y < file_buffer->num_lines) {
                    cursor_y++;
                    if(cursor_y > screen->dimension_y){
                        screen->y_index++;
                        display_file(file_buffer);
                    }
                }
            }else if(c == 127 || c == 8){
                if (cursor_x > 1) {
                    delete_char(file_buffer, cursor_y - 1, cursor_x - 2);
                    re_render_line(cursor_y - 1,file_buffer, cursor_x - 2);
                    cursor_x--;
                    changes--;
                } else if (cursor_x == 1) {
                    merge_lines(file_buffer, (size_t)(cursor_y - 1));
                    if(screen->y_index > 0){
                        screen->y_index--;
                    }
                    display_file(file_buffer);
                    if(cursor_y > 1){
                        changes--;
                    }
                }
            } else if(c == 13 || c == 10) {
                file_buffer = add_new_line(file_buffer,cursor_y - 1, cursor_x - 1);
                cursor_y++;
                if(cursor_y > screen->dimension_y){
                    cursor_x = 1;
                    screen->y_index++;
                    display_file(file_buffer);
                } else {
                    cursor_x = 1;
                    display_file(file_buffer);
                }
            } else if(c >= 32 && c < 127){
                size_t col = insert_char(file_buffer, cursor_y  - 1, cursor_x - 1, c);
                re_render_line(cursor_y - 1,file_buffer,col);
                cursor_x = col + 2;
                changes++;
            }
            if(prev_changes != changes){
                printf("\x1b[%zu;%zuH\x1b[K",screen->dimension_y+3, 1);
                printf("Length change: %d - \x1b[90mUnsaved\x1b[0m", changes);
            }
        } else if(is_command_mode == 1){
            //handles command mode
            if (c == 27) {
                if (getchar() != '[') continue;
                int k = getchar();
                if (k == 'D' && command_mode_cursor_x > 1) command_mode_cursor_x--;
                else if (k == 'C' && command_mode_cursor_x < command->length + 1) command_mode_cursor_x++;
            } else if((c == 127 || c == 8)) {
                if (command_mode_cursor_x > 1) {
                    delete_char_command_palette(command_mode_cursor_x - 2);
                    command_mode_cursor_x--;
                }
                display_command_palette();
            } else if(c == 13 || c == 10) {
                if(command->length > 0){
                    evaluate_command(file_buffer);
                    display_command_palette();
                }
            } else if(c >= 32 && c < 127){
                size_t col = insert_char_command_palette(command_mode_cursor_x - 1, c);
                command_mode_cursor_x = col + 2;
                display_command_palette();
            }
        }
        printf("\x1b[%zu;%zuH\x1b[?25h",(is_command_mode == 1) ? (command_mode_cursor_y) : (cursor_y - screen->y_index),(is_command_mode == 1) ? command_mode_cursor_x : (cursor_x + padding_x));
    }
    //uninitialize the terminal and free memory
    free(file_buffer);
    free_command_palette();
    return EXIT_SUCCESS;
}