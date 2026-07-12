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

    Pager* pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = file_length;
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
        uint32_t num_pages = pager->file_length / PAGE_SIZE;

        // A partial page at the end of the file still counts as one page.
        if (pager->file_length % PAGE_SIZE) {
            num_pages += 1;
        }

        if (page_num < num_pages) {
            // This page has data on disk — seek to its offset and read it.
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            read(pager->file_descriptor, page, PAGE_SIZE);
        }
        // New pages (page_num >= num_pages) are left as uninitialized memory;
        // they will be filled by insert and written to disk on db_close.
        pager->pages[page_num] = page;
    }
    return pager->pages[page_num];
}

/*
 * pager_flush - Write `size` bytes of a cached page back to disk.
 *
 * We seek to the page's position in the file (page_num * PAGE_SIZE)
 * and write exactly `size` bytes. For full pages size == PAGE_SIZE.
 * For the last partial page, size == (num_rows_on_page * ROW_SIZE)
 * so we don't write uninitialized bytes past the last real row.
 */
void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
    if (pager->pages[page_num] == NULL) {
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }
    lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    write(pager->file_descriptor, pager->pages[page_num], size);
}

/*
 * row_slot - Return a pointer to where row `row_num` lives in memory.
 *
 * Layout math:
 *   page_num   = row_num / ROWS_PER_PAGE   -> which page holds this row
 *   row_offset = row_num % ROWS_PER_PAGE   -> which slot within that page
 *   byte_offset = row_offset * ROW_SIZE    -> byte position inside the page
 *
 * get_page handles loading the page from disk if it isn't cached yet.
 * The returned pointer is used directly by serialize/deserialize to
 * read or write the row in place.
 */
void* row_slot(Table* table, uint32_t row_num) {
    uint32_t page_num   = row_num / ROWS_PER_PAGE;
    void*    page       = get_page(table->pager, page_num);
    uint32_t row_offset = row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;
    return page + byte_offset;
}

/*
 * db_open - Open a database file and return a Table ready to use.
 *
 * Delegates file handling to pager_open, then figures out how many
 * rows are already stored by dividing the file size by ROW_SIZE.
 * That count is used by INSERT to know where to append the next row,
 * and by SELECT to know how many rows to scan.
 */
Table* db_open(const char* filename) {
    Pager* pager = pager_open(filename);
    uint32_t num_rows = pager->file_length / ROW_SIZE;

    Table* table = malloc(sizeof(Table));
    table->pager = pager;
    table->num_rows = num_rows;
    return table;
}

/*
 * db_close - Flush all dirty pages to disk, then free all resources.
 *
 * This is the only point where data is written to the .db file.
 * Rows inserted during the session live only in pager->pages[] until here.
 *
 * Two-pass flush:
 *   1. Full pages: iterate over every complete page and flush PAGE_SIZE bytes.
 *   2. Partial page: if the row count doesn't divide evenly into pages,
 *      the last page is partially filled. Flush only the bytes that hold
 *      real rows (num_additional_rows * ROW_SIZE) to avoid writing garbage.
 *
 * After flushing, free each page buffer, close the file descriptor,
 * and free the Pager and Table structs themselves.
 */
void db_close(Table* table) {
    Pager* pager = table->pager;
    uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;

    // Flush every complete page.
    for (uint32_t i = 0; i < num_full_pages; i++) {
        if (pager->pages[i] == NULL) continue;  // never loaded, nothing to flush
        pager_flush(pager, i, PAGE_SIZE);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    // Flush the last partial page (if any rows don't fill a complete page).
    uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
    if (num_additional_rows > 0) {
        uint32_t page_num = num_full_pages;
        if (pager->pages[page_num] != NULL) {
            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
            free(pager->pages[page_num]);
            pager->pages[page_num] = NULL;
        }
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

/*
 * execute_insert - Append a new row to the table.
 *
 * Checks that the table isn't full, then serializes the row directly
 * into the memory slot for the next available row. Incrementing
 * table->num_rows is what "commits" the insert in memory — it will be
 * persisted to disk when db_close is called.
 */
static ExecuteResult execute_insert(Statement *statement, Table *table) {
    if (table->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }
    serialize_row(&(statement->row_to_insert), row_slot(table, table->num_rows));
    table->num_rows++;
    return EXECUTE_SUCCESS;
}

/*
 * execute_select - Scan and print every row in the table.
 *
 * A full table scan: iterates from row 0 to num_rows-1, deserializes
 * each row from its memory slot, and prints it. No filtering yet.
 */
static ExecuteResult execute_select(Statement *statement __attribute__((unused)), Table *table) {
    Row row;
    for (uint32_t i = 0; i < table->num_rows; i++) {
        deserialize_row(row_slot(table, i), &row);
        print_row(&row);
    }
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
