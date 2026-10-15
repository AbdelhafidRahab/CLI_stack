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

struct cell {
    char ch;
    int fg;
    int bold;
};

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

static struct termios orig_termios;
static int termios_saved = 0;

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

static int screen_param(struct screen *s, int index, int fallback) {
    if (index >= s->nparams) {
        return fallback;
    }
    if (s->params[index] == 0) {
        return fallback;
    }
    return s->params[index];
}

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

static int set_raw_input(void) {
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
        perror("tcgetattr");
        return -1;
    }

    termios_saved = 1;
    raw = orig_termios;
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

static void copy_winsize(int master) {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        ioctl(master, TIOCSWINSZ, &ws);
    }
}

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

    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = master;
    pfds[1].events = POLLIN;

    for (;;) {
        int ready = poll(pfds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        if (pfds[0].revents & POLLIN) {
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0) {
                break;
            }
            write_all(master, buf, (size_t)n);
        }

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