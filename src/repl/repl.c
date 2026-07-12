#include <stdio.h>
#include <stdbool.h>
#include "repl.h"
#include "compiler/parser.h"
#include "backend/vm.h"

void repl_start(const char *filename) {
    InputBuffer *input_buffer = new_input_buffer();
    Table *table = db_open(filename);

    while (true) {
        print_prompt();
        read_input(input_buffer);

        // 1. Meta command? (lines starting with '.')
        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, table)) {
                case META_COMMAND_SUCCESS:
                    continue;
                case META_COMMAND_UNRECOGNIZED_COMMAND:
                    printf("Unrecognized command '%s'.\n", input_buffer->buffer);
                    continue;
            }
        }

        // 2. Otherwise it's a SQL statement: prepare, then execute.
        Statement statement;
        switch (prepare_statement(input_buffer, &statement)) {
            case PREPARE_SUCCESS:
                break;
            case PREPARE_SYNTAX_ERROR:
                printf("Syntax error. Could not parse statement.\n");
                continue;
            case PREPARE_UNRECOGNIZED_STATEMENT:
                printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
                continue;
        }

        switch (execute_statement(&statement, table)) {
            case EXECUTE_SUCCESS:
                printf("Executed.\n");
                break;
            case EXECUTE_TABLE_FULL:
                printf("Error: Table full.\n");
                break;
        }
    }
}
