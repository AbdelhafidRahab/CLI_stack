/* uth_grep
   Print each line that contains the pattern as plain text.
   err matches "error". . and * are not special.
   No file means read the keyboard, or whatever a pipe fed us. */

#include <stdio.h>
#include <string.h>

#define LINE_MAX 4096

/* strstr finds the pattern anywhere in the line. NULL means no match. */
static void grep_stream(FILE *in, const char *pattern) {
    char line[LINE_MAX];

    while (fgets(line, sizeof line, in) != NULL) {
        if (strstr(line, pattern) != NULL) {
            fputs(line, stdout);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: uth_grep pattern [file]\n");
        return 1;
    }

    if (argc == 2) {
        grep_stream(stdin, argv[1]);
        return 0;
    }

    if (argc != 3) {
        fprintf(stderr, "usage: uth_grep pattern [file]\n");
        return 1;
    }

    FILE *fp = fopen(argv[2], "r");
    if (fp == NULL) {
        perror(argv[2]);
        return 1;
    }

    grep_stream(fp, argv[1]);
    fclose(fp);
    return 0;
}