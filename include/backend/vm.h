#ifndef NOPALDB_VM_H
#define NOPALDB_VM_H

#include "common.h"
#include "compiler/parser.h"

// Helper macro to compute the size of a struct field
#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

// Row field sizes
#define ID_SIZE       size_of_attribute(Row, id)
#define USERNAME_SIZE size_of_attribute(Row, username)
#define EMAIL_SIZE    size_of_attribute(Row, email)

// Row field offsets within a serialized page slot
#define ID_OFFSET       0
#define USERNAME_OFFSET (ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET    (USERNAME_OFFSET + USERNAME_SIZE)
#define ROW_SIZE        (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)

// Page and table limits
#define PAGE_SIZE      4096  // 4 KB — matches typical OS page size
#define ROWS_PER_PAGE  (PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS (ROWS_PER_PAGE * TABLE_MAX_PAGES)

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;

Table *new_table(void);
void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);
void *row_slot(Table *table, uint32_t row_num);
void print_row(Row *row);
ExecuteResult execute_statement(Statement *statement, Table *table);

#endif /* NOPALDB_VM_H */
