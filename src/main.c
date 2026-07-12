#include <stdio.h>
#include "repl.h"


// make && ./build/nopaldb <filename>
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <db_file>\n", argv[0]);
        return 1;
    }
    repl_start(argv[1]);
}
