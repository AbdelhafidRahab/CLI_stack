# CLI Stack

CLI Stack is one project in a series I am building so I can understand how the software I use every day works under the hood.

This one is the command-line stack: the window you type in, the program that reads commands, and the small programs those commands run.

## What it contains

| Part | What it is | Status |
|------|------------|--------|
| Shell (`uthsh`) | Reads a command line and runs programs | Not started |
| CLI tools (`uth-echo`, `uth-cat`, …) | Small programs the shell runs | Not started |
| Terminal (`uth-term`) | Window that shows text and sends keys to the shell | Not started |

Built and tested on Linux.

## Requirements

- Linux
- GCC (`build-essential`)
- Make
- Git