#include <string.h>
#include <stdio.h>
#include "compiler/parser.h"

static PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
    statement->type = STATEMENT_INSERT;
    int args_assigned = sscanf(
        input_buffer->buffer, "insert %d %s %s",
        &(statement->row_to_insert.id),
        statement->row_to_insert.username,
        statement->row_to_insert.email
    );

    if (args_assigned < 3) {
        return PREPARE_SYNTAX_ERROR;
    }

    return PREPARE_SUCCESS;
}

// Prepare : Understand WHAT user wants
PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        return prepare_insert(input_buffer, statement);
    }
    if (strcmp(input_buffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}