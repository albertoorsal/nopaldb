#ifndef NOPALDB_PARSER_H
#define NOPALDB_PARSER_H

#include "common.h"
#include "repl.h"

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT,
    PREPARE_SYNTAX_ERROR
} PrepareResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct {
    StatementType type;
    Row row_to_insert;
} Statement;

PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);

#endif /* NOPALDB_PARSER_H */
