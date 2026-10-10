#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define LINE_MAX 4096
#define MAX_ARGS 64

/* Cut the line into words. argv must end with NULL. That is what execvp expects */
static int split_line(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < max_args - 1) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        argv[argc++] = p; // stores that pointer in the next slot, then adds 1 to argc

        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        *p = '\0';
        p++;
    }

    argv[argc] = NULL;
    return argc;
}

static int take_redirects(char *argv[], char **infile,char **outfile) {
    char *kept[MAX_ARGS];
    int n = 0;

    *infile = NULL;
    *outfile = NULL;

    for (int i = 0; argv[i] != NULL ; i++) {
        if (strcmp(argv[i], ">") == 0) {
            if (argv[i + 1] == NULL) {
                fprintf(stderr, "syntax error: > needs a file\n");
                return -1;
            }

            *outfile = argv[i + 1];
            i++;
            continue;
        }
        if (strcmp(argv[i], "<") == 0) {
            if (argv[i + 1] == NULL) {
                fprintf(stderr, "syntax error: < needs a file\n");
                return -1;
            }
            *infile = argv[i + 1];
            i++;
            continue;
        }
        kept[n++] = argv[i];
    }

    kept[n] = NULL;
    for (int i = 0; i <= n; i++) {
        argv[i] = kept[i];
    }

    return n;
}

static int apply_redirects(const char *infile, const char *outfile) {
    if (infile != NULL) {
        /*
            O_RDONLY means read only. There is no O_CREAT. If the file is missing, open fails and perror prints the reason.
        */
        int fd = open(infile, O_RDONLY);
        if (fd < 0) {
            perror(infile);
            return -1;
        }
        /*
            dup2(fd, STDIN_FILENO) makes descriptor 0 refer to that file. The child no longer reads the keyboard. 
        */
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (outfile != NULL) {
        /* 
            This | means “turn on all of these bits”.
            - O_WRONLY : write only. This descriptor cannot read the file.
            - O_CREAT : if the path does not exist, create it.
            - O_TRUNC : if the path exists, make it empty before writing.
        */

        /*
            0644, is the permission used only when the file is created.
            - The leading 0 means this number is octal (base 8), not decimal.
            - The three digits are owner, group, everyone else. 6 is read+write for you. 4 is read-only for your group. 4 is read-only for everyone else. 
        */
        int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        // If open fails, it returns -1.
        if (fd < 0) {
            perror(outfile);
            return -1;
        }
        // makes descriptor 1 refer to the same open file as fd. After this, printf, fputs, and putchar in the child go into the file. The terminal is no longer descriptor 1 in that child.
        // Then close(fd). Descriptor 1 already points at the file, so the extra number is not needed. Close it. 
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int split_pipe(char *argv[], char *left[], char *right[]) {
    int bars = 0;
    int at = -1;

    for (int i = 0; argv[i] != NULL; i++) {
        if (strcmp(argv[i], "|") == 0) {
            bars++;
            at = i;
        }
    }

    if (bars == 0) {
        return 0;
    }

    if (bars > 1) {
        fprintf(stderr, "only one | in this phase\n");
        return -1;
    }

    if (at == 0 || argv[at + 1] == NULL) {
        fprintf(stderr, "syntax error: | needs a command on each side\n");
        return -1;
    }

    int n = 0;
    for (int i = 0; i < at; i++) {
        left[n++] = argv[i];
    }
    left[n] = NULL;

    n = 0;
    for (int i = at + 1; argv[i] != NULL; i++) {
        right[n++] = argv[i];
    }
    right[n] = NULL;
    return 1;
}

static void run_pipe(char *left[], const char *left_in, const char *left_out, char *right[], const char *right_in, const char *right_out) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    pid_t left_pid = fork();
    if (left_pid < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (left_pid == 0) {
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);

        if (apply_redirects(left_in, left_out) < 0) {
            _exit(1);
        }
        execvp(left[0], left);
        perror(left[0]);
        _exit(127);
    }

    pid_t right_pid = fork();
    if (right_pid < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        int status = 0;
        waitpid(left_pid, &status, 0);
        return;
    }

    if (right_pid == 0) {
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        if (apply_redirects(right_in, right_out) < 0) {
            _exit(1);
        }
        execvp(right[0], right);
        perror(right[0]);
        _exit(127);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    int status = 0;
    if (waitpid(left_pid, &status, 0) < 0) {
        perror("waitpid");
    }
    if (waitpid(right_pid, &status, 0) < 0) {
        perror("waitpid");
    }
}

static void run_command(char *argv[], const char *infile, const char *outfile) {
    pid_t pid = fork();

    /* If the clone failed, pid is -1 */
    if (pid < 0) {
        perror("fork");
        return;
    }

    /* 
        Child: become the program the user named 
        This block runs in the child only, because only the child got 0 from fork
    */
    if (pid == 0) {
        if (apply_redirects(infile, outfile) < 0) {
            _exit(1);
        }
        // If execvp works, it never returns. The child is no longer uthsh. It is ls, or uth-echo, or whatever the user typed.
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }

    /* Parent: wait for that one child */
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
    }
}

static int is_builtin(const char *name) {
    return strcmp(name, "exit") == 0 || strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0;
}

static int handle_builtin(char *argv[]) {
    if (strcmp(argv[0], "exit") == 0) {
        return 1;
    }

    if (strcmp(argv[0], "cd") == 0) {
        const char *path = argv[1];

        if (path == NULL) {
            path = getenv("HOME");
            if (path == NULL) {
                fprintf(stderr, "cd: HOME is not set\n");
                return 0;
            }
        }

        if (chdir(path) != 0) {
            perror("cd");
        }

        return 0;
    }

    if (strcmp(argv[0], "pwd") == 0) {
        char cwd[4096];
        if (getcwd(cwd, sizeof cwd) == NULL) {
            perror("pwd");
            return 0;
        }
        printf("%s\n", cwd);
        return 0;
    }

    return -1; /* not a builtin — run it as a program */
}

int main(void) {
    char line[LINE_MAX];
    
    for (;;) {
        fputs("uthsh> ", stdout);
        /* pushes that text to the screen immediately. Without it, the prompt can sit in a buffer and stay invisible until more text is printed later */
        fflush(stdout);

        if (fgets(line, sizeof line, stdin) == NULL) {
            /* User pressed Ctrl-D, or input ended. */
            putchar('\n');
            break;
        }

        size_t n = strlen(line);
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }

        char *argv[MAX_ARGS];
        int argc = split_line(line, argv, MAX_ARGS);
        if (argc == 0) {
            continue;
        }

        char *left[MAX_ARGS];
        char *right[MAX_ARGS];
        int piped = split_pipe(argv, left, right);
        if (piped < 0) {
            continue;
        }
        if (piped == 1) {
            char *left_in = NULL;
            char *left_out = NULL;
            char *right_in = NULL;
            char *right_out = NULL;
            if (take_redirects(left, &left_in, &left_out) < 0) {
                continue;
            }
            if (take_redirects(right, &right_in, &right_out) < 0) {
                continue;
            }
            if (left[0] == NULL || right[0] == NULL) {
                fprintf(stderr, "syntax error: | needs a command on each side\n");
                continue;
            }
            run_pipe(left, left_in, left_out, right, right_in, right_out);
            continue;
        }

        char *infile = NULL;
        char *outfile = NULL;
        if (take_redirects(argv, &infile, &outfile) < 0) {
            continue;
        }

        if (argv[0] == NULL) {
            fprintf(stderr, "syntax error: missing command\n");
            continue;
        }

        if ((infile != NULL || outfile != NULL) && is_builtin(argv[0])) {
            fprintf(stderr, "redirection on a builtin is not exist yet\n");
            continue;
        }

        int builtin = handle_builtin(argv);
        if (builtin == 1) {
            break;
        }
        if (builtin == 0) {
            continue;
        }

        run_command(argv, infile, outfile);
    }

    return 0;
}