/* uth_echo
   Print the words after the program name, separated by spaces.
   uth_echo hello world  ->  hello world
   With no words, print only the newline. */

#include <stdio.h>

int main(int argc, char *argv[]) {
    /* argv[0] is the program name, we print only the words after it. */
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            putchar(' ');
        }
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return 0;
}