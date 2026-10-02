# CLI Stack

CLI Stack is one project in a series I am building so I can understand how the software I use every day works under the hood.

This one is the command-line stack: the window you type in, the program that reads commands, and the small programs those commands run.

## What it contains

| Part | What it is | Status |
|------|------------|--------|
| Shell (`uthsh`) | Reads a command line and runs programs. Builtins: `exit`, `cd`, `pwd` | Working |
| CLI tools | Small programs the shell runs | `uth_echo`, `uth_cat`, `uth_head`, `uth_tail`, `uth_grep`, `uth_wc` work |
| Terminal (`uth-term`) | Window that shows text and sends keys to the shell | Not started |

Built and tested on Linux.

Names use `uth` so these programs do not overwrite the system `echo`, `cat`, `head`, `tail`, `grep`, `wc`, or shell.

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

## Build and run

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
./bin/uthsh
```

Inside `uthsh`, `ls` starts the system `ls`. A name with a slash starts a tool in this repo, for example `bin/uth_cat Tools/sample.txt`. `cd`, `pwd`, and `exit` are handled by the shell itself.

## Requirements

- Linux
- GCC (`build-essential`)
- Make
- Git
