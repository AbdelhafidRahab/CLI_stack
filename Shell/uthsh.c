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

static int take_redirects(char *argv[], char **outfile) {
    char *kept[MAX_ARGS];
    int n = 0;

    *outfile = NULL;

    for (int i = 0; argv[i] != NULL ; i++) {
        if (strcmp(argv[i], ">") == 0) {
            if (argv[i +1] == NULL) {
                fprintf(stderr, "syntax error: > needs a file\n");
                return -1;
            }

            *outfile = argv[i + 1];
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

static int apply_redirects(const char *outfile) {
    if (outfile == NULL) {
        return 0;
    }

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
    return 0;

}

static void run_command(char *argv[], const char *outfile) {
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
        if (apply_redirects(outfile) < 0) {
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

        char *outfile = NULL;
        if (take_redirects(argv, &outfile) < 0) {
            continue;
        }

        if (argv[0] == NULL) {
            fprintf(stderr, "syntax error: missing command\n");
            continue;
        }

        if (outfile != NULL && is_builtin(argv[0])) {
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

        run_command(argv, outfile);
    }

    return 0;
}