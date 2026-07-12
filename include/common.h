#ifndef NOPALDB_COMMON_H
#define NOPALDB_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define TABLE_MAX_PAGES      100
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE    255

typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];  // +1 for the '\0' terminator
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

typedef struct {
    int file_descriptor; // OS handle to the open file
    uint32_t file_length; // size of the file in bytes
    void* pages[TABLE_MAX_PAGES]; // the page cache
} Pager;

typedef struct {
    Pager* pager;
    uint32_t num_rows;
} Table;

#endif /* NOPALDB_COMMON_H */
