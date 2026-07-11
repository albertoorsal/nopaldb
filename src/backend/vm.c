#include <stdio.h>
#include "backend/vm.h"

void execute_statement(Statement *statement) {
    switch (statement->type) {
        case STATEMENT_INSERT:
            printf("This is where we would do an insert.\n");
            break;
        case STATEMENT_SELECT:
            printf("This is where we would do an select.\n");
            break;
    }
}
