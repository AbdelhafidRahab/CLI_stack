#include <stdio.h>

#define LINE_MAX 4096
#define HEAD_LINES 10

static void print_head(FILE *in) {
    char line[LINE_MAX];
    int printed = 0;

    while (printed < HEAD_LINES && fgets(line, sizeof line, in) != NULL) {
        fputs(line, stdout);
        printed++;
    }
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        print_head(stdin);
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "usage: uth_head [file]\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");

    if (fp == NULL) {
        perror(argv[1]);
        return 1;
    }

    print_head(fp);
    fclose(fp);

    return 0;
}