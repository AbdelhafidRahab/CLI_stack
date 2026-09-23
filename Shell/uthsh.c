#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

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

static void run_command(char *argv[]) {
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

        int builtin = handle_builtin(argv);
        if (builtin == 1) {
            break;
        }
        if (builtin == 0) {
            continue;
        }

        run_command(argv);
    }

    return 0;
}