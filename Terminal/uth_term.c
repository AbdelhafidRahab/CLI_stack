#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;
static int termios_saved = 0;

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
            write_all(STDOUT_FILENO, buf, (size_t)n);
        }
    }

    restore_termios();
    close(master);
    int status = 0;
    waitpid(pid, &status, 0);
    return 0;
}