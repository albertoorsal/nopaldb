#ifndef NOPALDB_PARSER_H
#define NOPALDB_PARSER_H

#include "repl.h"

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct {
    StatementType type;
} Statement;

PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);

#endif /* NOPALDB_PARSER_H */
