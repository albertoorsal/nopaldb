#ifndef NOPALDB_VM_H
#define NOPALDB_VM_H

#include "common.h"
#include "compiler/parser.h"

// Row field offsets within a serialized page slot
#define ID_OFFSET       0
#define USERNAME_OFFSET (ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET    (USERNAME_OFFSET + USERNAME_SIZE)


typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL,
    EXECUTE_DUPLICATE_KEY
} ExecuteResult;

typedef struct {
    Table* table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;  // true when positioned one past the last element
} Cursor;

Pager *pager_open(const char *filename);
void *get_page(Pager *pager, uint32_t page_num);
void pager_flush(Pager *pager, uint32_t page_num);
Table *db_open(const char *filename);
void db_close(Table *table);
void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);
void print_row(Row *row);
void print_leaf_node(void *node);
ExecuteResult execute_statement(Statement *statement, Table *table);
uint32_t *leaf_node_num_cells(void *node);
void *leaf_node_cell(void *node, uint32_t cell_num);
uint32_t *leaf_node_key(void *node, uint32_t cell_num);
void *leaf_node_value(void *node, uint32_t cell_num);
void initialize_leaf_node(void *node);
void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value);
Cursor *table_start(Table *table);
Cursor *table_end(Table *table);
Cursor *table_find(Table *table, uint32_t key);
Cursor *leaf_node_find(Table *table, uint32_t page_num, uint32_t key);
void *cursor_value(Cursor *cursor);
void cursor_advance(Cursor *cursor);

#endif /* NOPALDB_VM_H */
