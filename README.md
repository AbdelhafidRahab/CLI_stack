# CLI Stack

CLI Stack is one project in a series I am building so I can understand how the software I use every day works under the hood.

This one is the command-line stack: the window you type in, the program that reads commands, and the small programs those commands run.

## What it contains

| Part | What it is | Status |
|------|------------|--------|
| Shell (`uthsh`) | Reads a command line and runs programs. Builtins: `exit`, `cd`, `pwd` | Working |
| CLI tools (`uth_echo`, `uth-cat`, …) | Small programs the shell runs | `uth_echo` works. `uth-cat` not started |
| Terminal (`uth-term`) | Window that shows text and sends keys to the shell | Not started |

Built and tested on Linux.

Names use `uth` so these programs do not overwrite the system `echo`, `cat`, or shell.

## Layout

```text
CLI_stack/
├── Shell/uthsh.c        # the shell
├── Tools/uth_echo.c     # prints the words passed after the program name
├── bin/                 # compiled programs (not in git)
└── README.md
```

## Build and run

From the repo root:

```bash
mkdir -p bin
gcc -std=c11 -Wall -Wextra -o bin/uth_echo Tools/uth_echo.c
gcc -std=c11 -Wall -Wextra -o bin/uthsh Shell/uthsh.c
./bin/uthsh
```

Inside `uthsh`, `ls` starts the system `ls`. `bin/uth_echo hello` starts the tool in this repo. `cd`, `pwd`, and `exit` are handled by the shell itself.

## Requirements

- Linux
- GCC (`build-essential`)
- Make
- Git
