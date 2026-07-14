#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "backend/vm.h"

/*
 * serialize_row - Copy a Row struct into a compact binary buffer.
 *
 * Rows are stored on disk in a fixed-layout binary format:
 *   [id (4 bytes)][username (33 bytes)][email (256 bytes)]
 *
 * We can't just memcpy the whole struct because the compiler may add
 * padding between fields. Instead we copy each field individually at
 * its known offset so the on-disk layout is always predictable.
 */
void serialize_row(Row* source, void* destination) {
    memcpy(destination + ID_OFFSET,       &(source->id),       ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET,    &(source->email),    EMAIL_SIZE);
}

/*
 * deserialize_row - Read a compact binary buffer back into a Row struct.
 *
 * The inverse of serialize_row. Pulls each field out of its fixed
 * offset in the buffer and writes it into the destination Row.
 */
void deserialize_row(void* source, Row* destination) {
    memcpy(&(destination->id),       source + ID_OFFSET,       ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email),    source + EMAIL_OFFSET,    EMAIL_SIZE);
}

/*
 * pager_open - Open (or create) the database file and initialize a Pager.
 *
 * The Pager is the layer between the table logic and the OS file system.
 * It owns the file descriptor and the in-memory page cache (pages[]).
 *
 * On open:
 *   - The file is opened for read/write, and created if it doesn't exist.
 *   - lseek to the end gives us the current file size, which tells us
 *     how many rows are already stored.
 *   - All cache slots start as NULL (nothing loaded into RAM yet).
 */
Pager* pager_open(const char* filename) {
    int fd = open(filename,
                  O_RDWR |      // read & write
                  O_CREAT,      // create if it doesn't exist
                  S_IWUSR |     // user write permission
                  S_IRUSR);     // user read permission
    if (fd == -1) {
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }
    off_t file_length = lseek(fd, 0, SEEK_END);  // seek to end = get file size

    if (file_length % PAGE_SIZE != 0) {
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        exit(EXIT_FAILURE);
    }

    Pager* pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = file_length;
    pager->num_pages = file_length / PAGE_SIZE;
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;   // nothing cached yet
    }
    return pager;
}

/*
 * get_page - Return a pointer to a page in the cache, loading it from
 *            disk first if it isn't already in memory (lazy loading).
 *
 * Pages are 4 KB blocks. Each page holds ROWS_PER_PAGE rows.
 * The cache is pager->pages[], indexed by page number.
 *
 * Cache miss path:
 *   1. Allocate a 4 KB buffer.
 *   2. If the page exists on disk (page_num < num_pages), seek to its
 *      position and read it into the buffer.
 *   3. Store the buffer in the cache so future accesses skip the disk read.
 *
 * Cache hit path:
 *   Return the already-loaded pointer directly.
 */
void* get_page(Pager* pager, uint32_t page_num) {
    if (page_num > TABLE_MAX_PAGES) {
        printf("Tried to fetch page number out of bounds.\n");
        exit(EXIT_FAILURE);
    }

    if (pager->pages[page_num] == NULL) {
        // Cache miss: allocate a page and load from file if it exists.
        void* page = malloc(PAGE_SIZE);

        if (page_num < pager->num_pages) {
            // This page has data on disk — seek to its offset and read it.
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            read(pager->file_descriptor, page, PAGE_SIZE);
        }
        // New pages beyond num_pages are left uninitialized;
        // they will be initialized by the caller and flushed on db_close.
        pager->pages[page_num] = page;
        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }
    return pager->pages[page_num];
}

/*
 * pager_flush - Write a full page back to disk.
 *
 * B-tree pages are always PAGE_SIZE — no partial pages.
 */
void pager_flush(Pager* pager, uint32_t page_num) {
    if (pager->pages[page_num] == NULL) {
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }
    lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);
}

/*
 * db_open - Open a database file and return a Table ready to use.
 *
 * The tree tracks its own size via the page structure, so Table only
 * needs to know the root page number. If the file is brand new,
 * page 0 is initialized as an empty leaf node.
 */
Table* db_open(const char* filename) {
    Pager* pager = pager_open(filename);

    Table* table = malloc(sizeof(Table));
    table->pager = pager;
    table->root_page_num = 0;

    if (pager->num_pages == 0) {
        // Brand-new file: initialize page 0 as an empty leaf node.
        void* root_node = get_page(pager, 0);
        initialize_leaf_node(root_node);
    }
    return table;
}

/*
 * db_close - Flush all pages to disk, then free all resources.
 *
 * B-tree pages are always full PAGE_SIZE — no partial pages.
 * Loop over every page the pager knows about and flush each one.
 */
void db_close(Table* table) {
    Pager* pager = table->pager;

    for (uint32_t i = 0; i < pager->num_pages; i++) {
        if (pager->pages[i] == NULL) continue;  // never loaded, nothing to flush
        pager_flush(pager, i);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    close(pager->file_descriptor);
    free(pager);
    free(table);
}

/*
 * print_row - Print a single row to stdout in (id, username, email) format.
 */
void print_row(Row *row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

void print_leaf_node(void* node) {
    uint32_t num_cells = *leaf_node_num_cells(node);
    printf("leaf (size %d)\n", num_cells);
    for (uint32_t i = 0; i < num_cells; i++) {
        printf("  - %d : %d\n", i, *leaf_node_key(node, i));
    }
}

/*
 * execute_insert - Insert a new row into the correct sorted position.
 *
 * Uses leaf_node_find (binary search) to locate where the key belongs.
 * If the key already exists, returns EXECUTE_DUPLICATE_KEY.
 */
static ExecuteResult execute_insert(Statement *statement, Table *table) {
    void* node = get_page(table->pager, table->root_page_num);
    if (*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS) {
        return EXECUTE_TABLE_FULL;
    }
    Row* row = &(statement->row_to_insert);
    uint32_t key = row->id;
    Cursor* cursor = table_find(table, key);

    // Duplicate key check: if the cursor landed on an existing cell with
    // the same key, reject the insert.
    if (cursor->cell_num < *leaf_node_num_cells(node) &&
        *leaf_node_key(node, cursor->cell_num) == key) {
        free(cursor);
        return EXECUTE_DUPLICATE_KEY;
    }

    leaf_node_insert(cursor, key, row);
    free(cursor);
    return EXECUTE_SUCCESS;
}

/*
 * execute_select - Scan and print every row in the table.
 *
 * A full table scan: iterates from row 0 to num_rows-1, deserializes
 * each row from its memory slot, and prints it. No filtering yet.
 */
static ExecuteResult execute_select(Statement *statement __attribute__((unused)), Table *table) {
    Cursor* cursor = table_start(table);
    Row row;
    while (!cursor->end_of_table) {
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);
        cursor_advance(cursor);
    }
    free(cursor);
    return EXECUTE_SUCCESS;
}

/*
 * execute_statement - Dispatch a prepared statement to its handler.
 *
 * The parser produces a Statement with a type tag. This function
 * routes it to execute_insert or execute_select accordingly.
 */
ExecuteResult execute_statement(Statement *statement, Table *table) {
    switch (statement->type) {
        case STATEMENT_INSERT:
            return execute_insert(statement, table);
        case STATEMENT_SELECT:
            return execute_select(statement, table);
    }
}


/**
 * Accessor functions to return pointer to right spot
 * 
 */
uint32_t* leaf_node_num_cells(void* node) {
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num);
}

void* leaf_node_value(void* node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

void initialize_leaf_node(void* node) {
    *(uint8_t*)(node + NODE_TYPE_OFFSET) = NODE_LEAF;
    *leaf_node_num_cells(node) = 0;
}

Cursor* table_start(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = table->root_page_num;
    cursor->cell_num = 0;
    void* root_node = get_page(table->pager, table->root_page_num);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor->end_of_table = (num_cells == 0);
    return cursor;
}

Cursor* table_end(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = table->root_page_num;
    void* root_node = get_page(table->pager, table->root_page_num);
    cursor->cell_num = *leaf_node_num_cells(root_node);
    cursor->end_of_table = true;
    return cursor;
}

void* cursor_value(Cursor* cursor) {
    void* page = get_page(cursor->table->pager, cursor->page_num);
    return leaf_node_value(page, cursor->cell_num);
}

void cursor_advance(Cursor* cursor) {
    void* node = get_page(cursor->table->pager, cursor->page_num);
    cursor->cell_num += 1;
    if (cursor->cell_num >= *leaf_node_num_cells(node)) {
        cursor->end_of_table = true;
    }
}

/*
 * leaf_node_find - Binary search a leaf node for `key`.
 *
 * Maintains two bounds [min_index, one_past_max_index) and halves the
 * range each iteration. Returns a cursor pointing at:
 *   - the cell whose key == `key` (duplicate), or
 *   - the cell where `key` should be inserted to keep the node sorted.
 */
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
    void* node = get_page(table->pager, page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = page_num;
    cursor->end_of_table = false;

    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;

    while (min_index < one_past_max_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *leaf_node_key(node, index);

        if (key == key_at_index) {
            cursor->cell_num = index;
            return cursor;
        }
        if (key < key_at_index) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    cursor->cell_num = min_index;
    return cursor;
}

/*
 * table_find - Return a cursor to the position of `key` in the tree.
 *
 * Dispatches to leaf_node_find for leaf nodes. Internal nodes are not
 * yet implemented and will abort.
 */
Cursor* table_find(Table* table, uint32_t key) {
    uint32_t root_page_num = table->root_page_num;
    void* root_node = get_page(table->pager, root_page_num);

    if (*(uint8_t*)(root_node + NODE_TYPE_OFFSET) == NODE_LEAF) {
        return leaf_node_find(table, root_page_num, key);
    } else {
        printf("Need to implement searching an internal node.\n");
        exit(EXIT_FAILURE);
    }
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        printf("Need to implement splitting a leaf node.\n");
        exit(EXIT_FAILURE);   // Lesson 6 fixes this
    }
    if (cursor->cell_num < num_cells) {
        // make room: shift later cells one slot to the right
        for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
                   LEAF_NODE_CELL_SIZE);
        }
    }
    *(leaf_node_num_cells(node)) += 1;
    *(leaf_node_key(node, cursor->cell_num)) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

