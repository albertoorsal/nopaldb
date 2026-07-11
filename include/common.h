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
    uint32_t num_rows;
    void *pages[TABLE_MAX_PAGES];
} Table;

#endif /* NOPALDB_COMMON_H */
