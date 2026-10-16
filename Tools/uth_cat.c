/* uth_cat
   Copy bytes to the screen. With no path, the bytes come from the
   keyboard (or from a pipe). With paths, each file is printed in order.
   A missing file is reported and the remaining files still run. */

#include <stdio.h>

/* Read one byte at a time until the stream ends. int, not char,
   because EOF is -1 and a real byte must not be confused with it. */
static void copy_stream(FILE *in) {
    int c;

    while ((c = fgetc(in)) != EOF) {
        putchar(c);
    }
}

int main(int argc, char *argv[]) {
    int status = 0;

    if (argc == 1) { // the input channel is the keyboard
        copy_stream(stdin);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");

        if (fp == NULL) {
            perror(argv[i]);
            status = 1;
            continue;
        }

        copy_stream(fp);
        fclose(fp);
    }

    return status;
}