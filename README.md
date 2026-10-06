# Cedit

Cedit is a simple **1-file terminal-based ide** that handles reading & writing files and a basic command palette.



## How does it work?

Cedit is based on a loop that waits to iterate until a keypress: then, based on the key, it edits contents and visually updates specific parts of the terminal window.
Since for now its only at ~700 lines of code, its really basic, and doesn't support a lot of shortcuts.
The goal of this project is to make a vim-like program, but easier to use and understand.

For now, to use cedit, you just need to add it to the PATH, and then use `cedit <filename>` to open (or create) a file.
When you open a file, you will immediatly see the file contents (or an empty screen if the file is created): the lines marked with a `~` are the lines the file has,
and the ones marked with a `#` are the ones outside of the file: to add lines, simply press `Enter`. To save the file press `Ctrl+S`, and to close the editor press `Ctrl+Q`.

> The file is displayed with a screen system of height 20 lines, so if your file is bigger then you can just scroll or move the cursor down to see the rest.

You may notice at the bottom a separator and under it two lines: the first one tracks the changes (so the characters added or removed) to the file since the last save, and the second one is the command palette.

## The command palette

As you will see written over it, **to enter and exit the command palette** press `Ctrl+P`.

If you enter it successfully, it will change its message to `Enter command...`: then, you can enter a command and press `Enter` to use it.

> Keep in mind, the program tracks 2 cursors, one for the file (the *file* cursor), and one for the command palette (the *command* cursor):
> it just toggles their position when you press `Ctrl+P`. This is used in some commands.

### The commands

Every command starts with a `$` symbol. This is a choice made to prevent occasional typos and unwanted commands. As of right now, there are just **5** commands, those being:

- (`$lns` or `$lines`): counts the lines of the program.

- (`$jmp` or `$jump`): when entered, it reads digits until you press `Enter`. It then **jumps** (moves the *file* cursor) to the specified line, if it can.

- (`$src` or `$search`): same as jump, but it reads printable chars instead of digits. When you press `Enter` again, it prints the lines that contain the characters specified.

- (`$cll` or `$clearline`): clears the line the *file* cursor is at.

- (`$swp` or `$swap`): swaps two lines, one is entered same as in the jump command, and the second one is the line the *file* cursor is at.

---

For now, this is it. The program only works on Windows, but i would really like if someone modified it to work on Linux the same. 

> To compile, download the source code and run `gcc -o /build/cedit main.c` 
