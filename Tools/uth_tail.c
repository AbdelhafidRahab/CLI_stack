/* uth_tail
   Print the last TAIL_LINES lines.
   The stream only moves forward, so the last lines are kept in a ring:
   once it is full, the next line overwrites the oldest slot.
   No -n yet. The count is fixed at 10. */

#include <stdio.h>
#include <string.h>

#define LINE_MAX 4096
#define TAIL_LINES 10

static void print_tail(FILE *in) {
    char lines[TAIL_LINES][LINE_MAX];
    char buf[LINE_MAX];
    int next = 0;
    int count = 0;

    while (fgets(buf, sizeof buf, in) != NULL) {
        strcpy(lines[next], buf);
        next = (next + 1) % TAIL_LINES;
        if (count < TAIL_LINES) {
            count++;
        }
    }

    /* If the ring never filled, line 0 is the oldest.
       If it filled, next is the slot that is about to be reused, which
       is also the oldest line still stored. */
    int start = (count < TAIL_LINES) ? 0 : next;
    for (int i = 0; i < count; i++) {
        fputs(lines[(start + i) % TAIL_LINES], stdout);
    }
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        print_tail(stdin);
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "usage: uth_tail [file]\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL) {
        perror(argv[1]);
        return 1;
    }

    print_tail(fp);
    fclose(fp);
    return 0;
}