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
#define PAGE_SIZE            4096  // 4 KB — matches typical OS page size

typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;

typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];  // +1 for the '\0' terminator
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

// Helper macro to compute the size of a struct field
#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

#define ID_SIZE       size_of_attribute(Row, id)
#define USERNAME_SIZE size_of_attribute(Row, username)
#define EMAIL_SIZE    size_of_attribute(Row, email)
#define ROW_SIZE      (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)

typedef struct {
    int file_descriptor; // OS handle to the open file
    uint32_t file_length; // size of the file in bytes
    uint32_t num_pages;
    void* pages[TABLE_MAX_PAGES]; // the page cache
} Pager;

typedef struct {
    Pager* pager;
    uint32_t num_rows;
    uint32_t root_page_num;
} Table;

// Common node header: type, is-root flag, pointer to parent
static const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
static const uint32_t NODE_TYPE_OFFSET = 0;
static const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
static const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
static const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
static const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
static const uint8_t  COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// Leaf node header: how many cells (key/value pairs) it holds
static const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
static const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
static const uint32_t LEAF_NODE_HEADER_SIZE =
    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;

// Leaf node body: an array of cells, each cell = key + serialized row
static const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
static const uint32_t LEAF_NODE_KEY_OFFSET = 0;
static const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
static const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
static const uint32_t LEAF_NODE_CELL_SIZE =
    LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
static const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
static const uint32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

/*
byte 0                                                        byte 4095
 ┌────────┬─────────┬───────────────┬──────────────────────────────────┐
 │ type   │ is_root │ parent ptr    │  num_cells │ cell 0 │ cell 1 │ …  │
 │ (1)    │ (1)     │ (4)           │  (4)       │ key+row│ key+row│    │
 └────────┴─────────┴───────────────┴──────────────────────────────────┘
   └──────── common header ────────┘└─leaf hdr─┘└──────── body ─────────┘
*/


#endif /* NOPALDB_COMMON_H */
