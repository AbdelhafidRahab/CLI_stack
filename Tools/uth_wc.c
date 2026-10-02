#include <stdio.h>

static void count_stream(FILE *in, int *lines, int *words, int *bytes) {
    int c;
    int in_word = 0; // is a flag. 0 means “I am not inside a word right now”. 1 means “the last bytes I saw are part of a word”

    *lines = 0;
    *words = 0;
    *bytes = 0;

    while ((c = fgetc(in)) != EOF) {
        (*bytes)++;

        if (c == '\n') {
            (*lines)++;
        }

        if (c == ' ' || c == '\t' || c == '\n') {
            in_word = 0;
        }else if (!in_word) { // means: this byte is not a separator, and we were not already inside a word. So this is the first byte of a new word. 
            in_word = 1;
            (*words)++;
        }
    }
}

int main(int argc, char *argv[]) {
    int lines = 0;
    int words = 0;
    int bytes = 0;

    if (argc == 1) {
        count_stream(stdin, &lines, &words, &bytes);
        printf("%d %d %d\n", lines, words, bytes);
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "usage: uth_wc [file]\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL) {
        perror(argv[1]);
        return 1;
    }

    count_stream(fp, &lines, &words, &bytes);
    fclose(fp);
    printf("%d %d %d %s\n", lines, words, bytes, argv[1]);
    return 0;
}