# CLI Stack

CLI Stack is one project in a series I am building so I can understand how the software I use every day works under the hood.

This one is the command-line stack: the window you type in, the program that reads commands, and the small programs those commands run.

Built and tested on Linux.

## What is here

| Part | Program | Status |
|------|---------|--------|
| Shell | `uthsh` | Works. Runs programs. Builtins: `exit`, `cd`, `pwd`. One `<`, one `>`, and one `\|` on a line |
| Tools | `uth_echo`, `uth_cat`, `uth_head`, `uth_tail`, `uth_grep`, `uth_wc` | Work |
| Terminal | `uth_term` | Works inside the terminal you start it from. It is not its own desktop window |

## Tools

| Tool | What it does |
|------|----------------|
| `uth_echo` | Prints the words passed after the program name |
| `uth_cat` | Prints the bytes of one or more files |
| `uth_head` | Prints the first 10 lines of a file |
| `uth_tail` | Prints the last 10 lines of a file |
| `uth_grep` | Prints the lines that contain a word |
| `uth_wc` | Counts lines, words, and bytes |

## Layout

```text
CLI_stack/
├── Shell/uthsh.c
├── Tools/
│   ├── uth_echo.c
│   ├── uth_cat.c
│   ├── uth_head.c
│   ├── uth_tail.c
│   ├── uth_grep.c
│   ├── uth_wc.c
│   ├── sample.txt
│   └── long.txt
├── bin/                 # compiled programs (not in git)
└── README.md
```

## Build

From the repo root:

```bash
mkdir -p bin
gcc -std=c11 -Wall -Wextra -o bin/uth_echo Tools/uth_echo.c
gcc -std=c11 -Wall -Wextra -o bin/uth_cat Tools/uth_cat.c
gcc -std=c11 -Wall -Wextra -o bin/uth_head Tools/uth_head.c
gcc -std=c11 -Wall -Wextra -o bin/uth_tail Tools/uth_tail.c
gcc -std=c11 -Wall -Wextra -o bin/uth_grep Tools/uth_grep.c
gcc -std=c11 -Wall -Wextra -o bin/uth_wc Tools/uth_wc.c
gcc -std=c11 -Wall -Wextra -o bin/uthsh Shell/uthsh.c
gcc -std=c11 -Wall -Wextra -o bin/uth_term Terminal/uth_term.c
```

## Run the shell

```bash
./bin/uthsh
```

```text
uthsh> ls
uthsh> bin/uth_echo hello
uthsh> bin/uth_cat Tools/sample.txt
uthsh> bin/uth_wc < Tools/sample.txt
uthsh> bin/uth_echo hello > /tmp/hi.txt
uthsh> bin/uth_grep hello Tools/sample.txt | bin/uth_wc
uthsh> cd /tmp
uthsh> pwd
uthsh> exit
```

A name with a `/` is a path, so `bin/uth_echo` runs the tool in this repo. A name with no `/` is looked up on `PATH`, so `ls` is the system `ls`.

There must be spaces around `<`, `>`, and `|`. A line can have one of each. `cd`, `pwd`, and `exit` run inside the shell, and they do not take `<` or `>` yet.

## Run the terminal

```bash
./bin/uth_term
```

Start this from the repo root, because it runs `bin/uthsh`.

`uth_term` stays in the terminal where you launched it. It asks Linux for a fake terminal cable (a PTY), starts `uthsh` on the far end, and keeps its own grid of characters. Keys go to the shell. What the shell prints is stored in the grid, including a small set of color and cursor commands, then painted back. Dragging the window updates the size. `exit` in the shell returns you to the terminal you started from.

`vim` and other full-screen programs will not look right.

## Not in yet

- more than one `|` on a line
- `>>`, `2>`, quotes, `$NAME`, `*.c`
- `head -n` and `tail -n` (both are fixed at 10 lines)
- a separate desktop window for `uth_term`

## Requirements

- Linux
- GCC (`build-essential`)
- Make
- Git
