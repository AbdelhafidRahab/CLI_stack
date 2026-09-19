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

        run_command(argv);
    }

    return 0;
}