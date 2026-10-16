/* uth_term
   Runs inside the terminal you started it from. That terminal is only
   the glass. This program owns the grid and the fake keyboard cable.
   It asks Linux for a PTY, starts bin/uthsh on the slave end, and
   copies keys to that end. Bytes coming back are stored in cells
   (normal characters, plus a small set of color and cursor commands)
   and then painted to the glass.
   Start it from the repo root: it execs bin/uthsh, which is a relative path.
   _XOPEN_SOURCE must stay above the includes or posix_openpt stays hidden. */

#define _XOPEN_SOURCE 600
#define MAX_ROWS 64
#define MAX_COLS 200

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* One character on the grid, plus the look it was given when stored.
   fg is 30-37, or -1 for the normal color. bold is 0 or 1. */
struct cell {
    char ch;
    int fg;
    int bold;
};

/* The visible grid, capped at MAX_ROWS by MAX_COLS.
   cur_r / cur_c are 0-based. fg and bold are the look of the NEXT character.
   state is the escape parser: 0 normal, 1 we just saw ESC, 2 we are inside ESC [.
   params holds the numbers in that sequence. current is the number still being read.*/
struct screen {
    struct cell cells[MAX_ROWS][MAX_COLS];
    int rows;
    int cols;
    int cur_r;
    int cur_c;
    int fg;
    int bold;
    int state;
    int params[8];
    int nparams;
    int current;
    int has_digit;
    int question;
};

/* Saved keyboard settings, so exit can put the outer terminal back.
   winch_flag is set from the resize signal. The handler must not do
   more than that. volatile so the loop actually re-reads it. */
static struct termios orig_termios;
static int termios_saved = 0;
static volatile sig_atomic_t winch_flag = 0;

static void cell_clear(struct cell *cell) {
    cell->ch = ' ';
    cell->fg = -1;
    cell->bold = 0;
}

static void screen_clear(struct screen *s) {
    for (int r = 0; r < s->rows; r++) {
        for (int c = 0; c < s->cols; c++) {
            cell_clear(&s->cells[r][c]);
        }
    }
}

/* Clamp into the arrays. A 0-size window would make the cursor illegal. */
static void screen_init(struct screen *s, int rows, int cols) {
    if (rows < 1) {
        rows = 1;
    }
    if (cols < 1) {
        cols = 1;
    }
    if (rows > MAX_ROWS) {
        rows = MAX_ROWS;
    }
    if (cols > MAX_COLS) {
        cols = MAX_COLS;
    }
    s->rows = rows;
    s->cols = cols;
    s->cur_r = 0;
    s->cur_c = 0;
    s->fg = -1;
    s->bold = 0;
    s->state = 0;
    s->nparams = 0;
    s->current = 0;
    s->has_digit = 0;
    s->question = 0;
    screen_clear(s);
}

/* New size, cursor pulled back inside it, cells wiped.
   Keeping the old text across a resize leaves torn lines. */
static void screen_resize(struct screen *s, int rows, int cols) {
    if (rows < 1) {
        rows = 1;
    }
    if (cols < 1) {
        cols = 1;
    }
    if (rows > MAX_ROWS) {
        rows = MAX_ROWS;
    }
    if (cols > MAX_COLS) {
        cols = MAX_COLS;
    }
    s->rows = rows;
    s->cols = cols;
    if (s->cur_r >= s->rows) {
        s->cur_r = s->rows - 1;
    }
    if (s->cur_c >= s->cols) {
        s->cur_c = s->cols - 1;
    }
    screen_clear(s);
}

/* Cursor commands treat a missing number and a 0 as the default.
   ESC [ H is row 1, column 1. */
static int screen_param(struct screen *s, int index, int fallback) {
    if (index >= s->nparams) {
        return fallback;
    }
    if (s->params[index] == 0) {
        return fallback;
    }
    return s->params[index];
}

/* The m command: the look of the following characters, not a move.
   0 resets, 1 bold, 22 bold off, 30-37 foreground, 39 default foreground.
   Background and bright colors are ignored. */
static void screen_sgr(struct screen *s) {
    if (s->nparams == 0) {
        s->fg = -1;
        s->bold = 0;
        return;
    }
    for (int i = 0; i < s->nparams; i++) {
        int p = s->params[i];
        if (p == 0) {
            s->fg = -1;
            s->bold = 0;
        } else if (p == 1) {
            s->bold = 1;
        } else if (p == 22) {
            s->bold = 0;
        } else if (p >= 30 && p <= 37) {
            s->fg = p;
        } else if (p == 39) {
            s->fg = -1;
        }
    }
}

/* Used after a cursor-move command. screen_put does not use this:
   it is allowed to sit just past the last column so the next character wraps. */
static void screen_clamp(struct screen *s) {
    if (s->cur_r < 0) {
        s->cur_r = 0;
    }
    if (s->cur_c < 0) {
        s->cur_c = 0;
    }
    if (s->cur_r >= s->rows) {
        s->cur_r = s->rows - 1;
    }
    if (s->cur_c >= s->cols) {
        s->cur_c = s->cols - 1;
    }
}

/* Finished ESC [ ... letter.
   H/f place the cursor (numbers in the sequence are 1-based).
   A/B/C/D move up/down/right/left. J clears, K clears the rest of the row.
   A ? sequence was only consumed so its letters do not become cells. */
static void screen_csi(struct screen *s, unsigned char cmd) {
    if (s->question) {
        return;
    }
    if (cmd == 'H' || cmd == 'f') {
        s->cur_r = screen_param(s, 0, 1) - 1;
        s->cur_c = screen_param(s, 1, 1) - 1;
        screen_clamp(s);
        return;
    }
    if (cmd == 'A') {
        s->cur_r -= screen_param(s, 0, 1);
        screen_clamp(s);
        return;
    }
    if (cmd == 'B') {
        s->cur_r += screen_param(s, 0, 1);
        screen_clamp(s);
        return;
    }
    if (cmd == 'C') {
        s->cur_c += screen_param(s, 0, 1);
        screen_clamp(s);
        return;
    }
    if (cmd == 'D') {
        s->cur_c -= screen_param(s, 0, 1);
        screen_clamp(s);
        return;
    }
    if (cmd == 'J') {
        int mode = (s->nparams == 0) ? 0 : s->params[0];
        if (mode == 2) {
            screen_clear(s);
            return;
        }
        if (mode == 0) {
            for (int c = s->cur_c; c < s->cols; c++) {
                cell_clear(&s->cells[s->cur_r][c]);
            }
            for (int r = s->cur_r + 1; r < s->rows; r++) {
                for (int c = 0; c < s->cols; c++) {
                    cell_clear(&s->cells[r][c]);
                }
            }
        }
        return;
    }
    if (cmd == 'K') {
        for (int c = s->cur_c; c < s->cols; c++) {
            cell_clear(&s->cells[s->cur_r][c]);
        }
        return;
    }
    if (cmd == 'm') {
        screen_sgr(s);
    }
}

/* Drop the top row, shift the rest up, blank the last row. */
static void screen_scroll(struct screen *s) {
    for (int r = 0; r < s->rows - 1; r++) {
        for (int c = 0; c < s->cols; c++) {
            s->cells[r][c] = s->cells[r + 1][c];
        }
    }
    for (int c = 0; c < s->cols; c++) {
        cell_clear(&s->cells[s->rows - 1][c]);
    }
    s->cur_r = s->rows - 1;
}

static void screen_newline(struct screen *s) {
    s->cur_c = 0;
    s->cur_r++;
    if (s->cur_r >= s->rows) {
        screen_scroll(s);
    }
}

/* Store one visible character and the look that is current right now. */
static void screen_put(struct screen *s, char ch) {
    if (s->cur_c >= s->cols) {
        s->cur_c = 0;
        s->cur_r++;
        if (s->cur_r >= s->rows) {
            screen_scroll(s);
        }
    }
    s->cells[s->cur_r][s->cur_c].ch = ch;
    s->cells[s->cur_r][s->cur_c].fg = s->fg;
    s->cells[s->cur_r][s->cur_c].bold = s->bold;
    s->cur_c++;
}

static void parser_reset(struct screen *s) {
    s->nparams = 0;
    s->current = 0;
    s->has_digit = 0;
    s->question = 0;
}

/* One byte from the shell.
   state 1: the previous byte was ESC. '[' starts a sequence, anything else is dropped.
   state 2: digits and ';' build the numbers. A byte from '@' to '~' ends it.
   Otherwise the byte is a character or a simple control: newline, return, backspace, tab.
   unsigned char so a byte above 127 is not a negative number. */
static void screen_feed(struct screen *s, unsigned char ch) {
    if (s->state == 1) {
        if (ch == '[') {
            s->state = 2;
            parser_reset(s);
            return;
        }
        s->state = 0;
        return;
    }

    if (s->state == 2) {
        if (ch == '?') {
            s->question = 1;
            return;
        }
        if (ch >= '0' && ch <= '9') {
            s->current = s->current * 10 + (ch - '0');
            s->has_digit = 1;
            return;
        }
        if (ch == ';') {
            if (s->nparams < 8) {
                s->params[s->nparams++] = s->has_digit ? s->current : 0;
            }
            s->current = 0;
            s->has_digit = 0;
            return;
        }
        if (ch >= '@' && ch <= '~') {
            if (s->has_digit && s->nparams < 8) {
                s->params[s->nparams++] = s->current;
            }
            screen_csi(s, ch);
            s->state = 0;
            return;
        }
        return;
    }

    if (ch == 27) {
        s->state = 1;
        return;
    }
    if (ch == '\n') {
        screen_newline(s);
        return;
    }
    if (ch == '\r') {
        s->cur_c = 0;
        return;
    }
    if (ch == '\b') {
        if (s->cur_c > 0) {
            s->cur_c--;
        }
        return;
    }
    if (ch == '\t') {
        int next = s->cur_c + 1;
        while (next % 8 != 0) {
            next++;
        }
        if (next > s->cols) {
            next = s->cols;
        }
        s->cur_c = next;
        return;
    }
    if (ch >= 32 && ch < 127) {
        screen_put(s, (char)ch);
    }
}

static void restore_termios(void) {
    if (termios_saved) {
        /*
            tcsetattr writes terminal settings:
                - STDIN_FILENO : descriptor 0, the keyboard.
                - TCSAFLUSH : apply them now, and throw away any keys already typed but not yet read.
                - &orig_termios : the location of the saved settings.
        */
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }
}

/* write() can send fewer bytes than asked. Keep going until n are out.
   EINTR means a signal interrupted it; try the same write again. */
static void write_all(int fd, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        off += (size_t)w;
    }
}

/* Tell the outer terminal which pen to use for the next run of cells.
   These bytes go to the glass. They are not fed back into screen_feed. */
static void redraw_style(int fg, int bold) {
    char seq[32];
    int n;
    if (bold && fg >= 30 && fg <= 37) {
        n = snprintf(seq, sizeof seq, "\033[1;%dm", fg);
    } else if (fg >= 30 && fg <= 37) {
        n = snprintf(seq, sizeof seq, "\033[%dm", fg);
    } else if (bold) {
        n = snprintf(seq, sizeof seq, "\033[1m");
    } else {
        n = snprintf(seq, sizeof seq, "\033[0m");
    }
    write_all(STDOUT_FILENO, seq, (size_t)n);
}

/* Paint the grid. ESC [ row ; col H is 1-based, so cur_r + 1.
   Cells with the same look are sent as one run, then the cursor is parked. */
static void screen_redraw(struct screen *s) {
    write_all(STDOUT_FILENO, "\033[H", 3);
    for (int r = 0; r < s->rows; r++) {
        char seq[32];
        int n = snprintf(seq, sizeof seq, "\033[%d;1H", r + 1);
        write_all(STDOUT_FILENO, seq, (size_t)n);

        int c = 0;
        while (c < s->cols) {
            int fg = s->cells[r][c].fg;
            int bold = s->cells[r][c].bold;
            int start = c;
            while (c < s->cols
                   && s->cells[r][c].fg == fg
                   && s->cells[r][c].bold == bold) {
                c++;
            }
            redraw_style(fg, bold);
            for (int k = start; k < c; k++) {
                char ch = s->cells[r][k].ch;
                write_all(STDOUT_FILENO, &ch, 1);
            }
        }
    }
    write_all(STDOUT_FILENO, "\033[0m", 4);
    char seq[32];
    int n = snprintf(seq, sizeof seq, "\033[%d;%dH", s->cur_r + 1, s->cur_c + 1);
    write_all(STDOUT_FILENO, seq, (size_t)n);
}

/* Keys must reach uthsh one byte at a time, and this program must not
   echo them (the PTY echoes) or treat Ctrl-C as "kill me".
   Output flags are left alone. Clearing them makes each new line start
   further right than the one above it. */
static int set_raw_input(void) {
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
        perror("tcgetattr");
        return -1;
    }

    termios_saved = 1;
    raw = orig_termios;
    /* ECHO off, no line editing (ICANON), Ctrl-C is a byte (ISIG),
       Ctrl-S is a byte (IXON), Enter is not rewritten before we see it (ICRNL). */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        perror("tcsetattr");
        return -1;
    }
    return 0;
}

/* Ask Linux for a PTY. We keep the master end.
   O_NOCTTY: do not attach it to this process. The child attaches the slave.
   grantpt / unlockpt let us open that slave. ptsname's buffer can be
   overwritten later, so the path is copied into slave_name. */
static int open_master(char *slave_name, size_t slave_name_size) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);

    if (master < 0) {
        perror("posix_openpt");
        return -1;
    }
    if (grantpt(master) < 0) {
        perror("grantpt");
        close(master);
        return -1;
    }
    if (unlockpt(master) < 0) {
        perror("unlockpt");
        close(master);
        return -1;
    }

    char *name = ptsname(master);
    if (name == NULL) {
        perror("ptsname");
        close(master);
        return -1;
    }
    snprintf(slave_name, slave_name_size, "%s", name);
    return master;
}

/* Give the fake terminal the same rows and columns as the one we are in.
   Otherwise ls wraps at the wrong width. */
static void copy_winsize(int master) {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        ioctl(master, TIOCSWINSZ, &ws);
    }
}

/* Child: new session, slave becomes its keyboard and screen, then bin/uthsh.
   The child closes the master. If it kept that end open, we would never
   see "the shell exited" on a later read.
   _exit, not return: return would run the rest of main in the child. */
static pid_t start_shell(int master, const char *slave_name) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        close(master);

        if (setsid() < 0) {
            _exit(1);
        }
        int slave = open(slave_name, O_RDWR);
        if (slave < 0) {
            _exit(1);
        }
        if (ioctl(slave, TIOCSCTTY, 0) < 0) {
            _exit(1);
        }
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) {
            close(slave);
        }
        char *argv[] = {"bin/uthsh", NULL};
        execvp(argv[0], argv);
        _exit(127);
    }
    return pid;
}

/* SIGWINCH fires when the outer terminal is dragged.
   Only set the flag. ioctl and redraw happen back in the loop. */
static void on_winch(int signo) {
    (void)signo;
    winch_flag = 1;
}

int main(void) {
    char slave_name[128];
    int master = open_master(slave_name, sizeof slave_name);
    if (master < 0) {
        return 1;
    }

    copy_winsize(master);

    struct screen screen;
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        screen_init(&screen, ws.ws_row, ws.ws_col);
    } else {
        screen_init(&screen, 24, 80);
    }
    /* Clear the glass once, so the old prompt is not sitting under the grid. */
    write_all(STDOUT_FILENO, "\033[2J", 4);

    pid_t pid = start_shell(master, slave_name);
    if (pid < 0) {
        close(master);
        return 1;
    }

    if (set_raw_input() < 0) {
        close(master);
        int status = 0;
        waitpid(pid, &status, 0);
        return 1;
    }

    /* Wait on the keyboard and the master together. Reading only one of
       them would freeze whenever the other side had something to say. */
    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = master;
    pfds[1].events = POLLIN;

    signal(SIGWINCH, on_winch);

    for (;;) {
        int ready = poll(pfds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        /* Resize our grid, then tell the PTY. The shell hears that as SIGWINCH.
           The cable gets the real size. The grid still stops at MAX_ROWS / MAX_COLS. */
        if (winch_flag) {
            winch_flag = 0;
            struct winsize ws;
            if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
                screen_resize(&screen, ws.ws_row, ws.ws_col);
                ioctl(master, TIOCSWINSZ, &ws);
                write_all(STDOUT_FILENO, "\033[2J", 4);
                screen_redraw(&screen);
            }
        }

        /* Keys go straight to the shell. We do not put them in the grid.
           The shell (through the PTY) echoes them back, and that echo is what we draw. */
        if (pfds[0].revents & POLLIN) {
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0) {
                break;
            }
            write_all(master, buf, (size_t)n);
        }

        /* Bytes from the shell. POLLHUP means the slave closed: uthsh exited. */
        if (pfds[1].revents & (POLLIN | POLLHUP)) {
            char buf[256];
            ssize_t n = read(master, buf, sizeof buf);
            if (n <= 0) {
                break;
            }
            for (ssize_t i = 0; i < n; i++) {
                screen_feed(&screen, (unsigned char)buf[i]);
            }
            screen_redraw(&screen);
        }
    }

    restore_termios();
    close(master);
    int status = 0;
    waitpid(pid, &status, 0);
    return 0;
}