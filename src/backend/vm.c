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
        // Brand-new file: initialize page 0 as an empty leaf node and root.
        void* root_node = get_page(pager, 0);
        initialize_leaf_node(root_node);
        set_node_root(root_node, true);
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
    Row* row = &(statement->row_to_insert);
    uint32_t key = row->id;
    Cursor* cursor = table_find(table, key);

    void* node = get_page(table->pager, cursor->page_num);

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

uint32_t* leaf_node_next_leaf(void* node) {
    return node + LEAF_NODE_NEXT_LEAF_OFFSET;
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
    set_node_root(node, false);
    *leaf_node_num_cells(node) = 0;
    *leaf_node_next_leaf(node) = 0;  // 0 = no right sibling
}

void initialize_internal_node(void* node) {
    *(uint8_t*)(node + NODE_TYPE_OFFSET) = NODE_INTERNAL;
    set_node_root(node, false);
    *internal_node_num_keys(node) = 0;
}

uint32_t* node_parent(void* node) {
    return node + PARENT_POINTER_OFFSET;
}

bool is_node_root(void* node) {
    return *(uint8_t*)(node + IS_ROOT_OFFSET);
}

void set_node_root(void* node, bool is_root) {
    *(uint8_t*)(node + IS_ROOT_OFFSET) = is_root;
}

/*
 * get_node_max_key - Largest key stored under this node's subtree.
 *
 * For a leaf, that's the key of its last cell. For an internal node,
 * the rightmost key is a separator, not the true max — so we follow
 * the right child down until we hit a leaf.
 */
uint32_t get_node_max_key(void* node) {
    if (*(uint8_t*)(node + NODE_TYPE_OFFSET) == NODE_LEAF) {
        return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
    }
    return *internal_node_key(node, *internal_node_num_keys(node) - 1);
}

uint32_t* internal_node_num_keys(void* node) {
    return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}

uint32_t* internal_node_right_child(void* node) {
    return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

uint32_t* internal_node_cell(void* node, uint32_t cell_num) {
    return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t* internal_node_child(void* node, uint32_t child_num) {
    uint32_t num_keys = *internal_node_num_keys(node);
    if (child_num > num_keys) {
        printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
        exit(EXIT_FAILURE);
    }
    if (child_num == num_keys) {
        return internal_node_right_child(node);
    }
    return internal_node_cell(node, child_num);
}

uint32_t* internal_node_key(void* node, uint32_t key_num) {
    return (void*)internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

Cursor* table_start(Table* table) {
    // Use table_find with key 0 to descend to the leftmost position.
    Cursor* cursor = table_find(table, 0);
    void* node = get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);
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
        uint32_t next = *leaf_node_next_leaf(node);
        if (next == 0) {
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next;
            cursor->cell_num = 0;
        }
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
 * internal_node_find_child - Binary search an internal node's keys to
 * find which child index to follow for `key`.
 *
 * An internal node with n keys has n+1 children. Keys act as separators:
 * child[i] holds keys < key[i]. If key >= all keys, follow the rightmost
 * child (index == num_keys).
 */
static uint32_t internal_node_find_child(void* node, uint32_t key) {
    uint32_t num_keys = *internal_node_num_keys(node);
    uint32_t min_index = 0;
    uint32_t max_index = num_keys;  // rightmost child is at index num_keys

    while (min_index < max_index) {
        uint32_t index = (min_index + max_index) / 2;
        uint32_t key_to_right = *internal_node_key(node, index);
        if (key_to_right >= key) {
            max_index = index;
        } else {
            min_index = index + 1;
        }
    }
    return min_index;
}

/*
 * table_find - Return a cursor to the position of `key` in the tree.
 *
 * Descends from the root, following internal nodes via binary search,
 * until it reaches a leaf where leaf_node_find does the final lookup.
 */
Cursor* table_find(Table* table, uint32_t key) {
    uint32_t page_num = table->root_page_num;

    while (true) {
        void* node = get_page(table->pager, page_num);
        if (*(uint8_t*)(node + NODE_TYPE_OFFSET) == NODE_LEAF) {
            return leaf_node_find(table, page_num, key);
        }
        uint32_t child_index = internal_node_find_child(node, key);
        page_num = *internal_node_child(node, child_index);
    }
}

/*
 * create_new_root - Promote a split into a new root internal node.
 *
 * After a leaf split, the old root's contents have already been copied
 * into a freshly allocated left-child page. Here we:
 *   1. Grab both children (left = old root page, right = new page).
 *   2. Re-initialize page 0 as an internal node (the new root).
 *   3. Write one key (the max key of the left child) and two child
 *      pointers into the root.
 */
void create_new_root(Table* table, uint32_t right_child_page_num) {
    void* root = get_page(table->pager, table->root_page_num);
    void* right_child = get_page(table->pager, right_child_page_num);

    // Allocate a page for the left child and copy the old root into it.
    uint32_t left_child_page_num = table->pager->num_pages;
    void* left_child = get_page(table->pager, left_child_page_num);
    memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);

    // Re-purpose the old root page as an internal node.
    initialize_internal_node(root);
    set_node_root(root, true);
    *internal_node_num_keys(root) = 1;
    *internal_node_child(root, 0) = left_child_page_num;

    // Separator key = largest key currently in the left child.
    *internal_node_key(root, 0) = get_node_max_key(left_child);
    *internal_node_right_child(root) = right_child_page_num;

    // The root page number never moves — both children now point back to it.
    *node_parent(left_child) = table->root_page_num;
    *node_parent(right_child) = table->root_page_num;
}

/*
 * internal_node_insert - Insert a new child (and its separator key) into
 * an existing (non-root) parent internal node. Returns the index the new
 * child was placed at, so the caller can fix up the sibling to its left
 * (whose max key just shrank) using an index instead of a stale key search.
 *
 * Called after a child node splits and the parent already exists (i.e.
 * the split wasn't at the root, so create_new_root doesn't apply). Finds
 * where the child belongs via binary search on max keys, shifts cells
 * right to make room, and writes the new key/child pair in place.
 *
 * Splitting an internal node that is itself full is out of scope here —
 * that's the next level of this data structure's growth.
 */
uint32_t internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
    void* parent = get_page(table->pager, parent_page_num);
    void* child = get_page(table->pager, child_page_num);
    uint32_t child_max_key = get_node_max_key(child);

    // child_max_key equals the pre-split max of child's left sibling (the
    // node it just split off from), so this search lands on that sibling's
    // own cell index — the new child belongs one slot to its right.
    uint32_t sibling_index = internal_node_find_child(parent, child_max_key);

    uint32_t original_num_keys = *internal_node_num_keys(parent);
    if (original_num_keys >= INTERNAL_NODE_MAX_CELLS) {
        printf("Need to implement splitting internal node\n");
        exit(EXIT_FAILURE);
    }

    *internal_node_num_keys(parent) = original_num_keys + 1;

    uint32_t new_child_index;
    if (sibling_index == original_num_keys) {
        // Left sibling was the rightmost child: it becomes a regular cell
        // (keyed by its own, now-final, max), and the new child takes over
        // as right_child. The new child's own child-array position is one
        // past the last key.
        uint32_t old_right_child_page_num = *internal_node_right_child(parent);
        void* old_right_child = get_page(table->pager, old_right_child_page_num);
        *internal_node_key(parent, original_num_keys) = get_node_max_key(old_right_child);
        *internal_node_child(parent, original_num_keys) = old_right_child_page_num;
        *internal_node_right_child(parent) = child_page_num;
        new_child_index = original_num_keys + 1;
    } else {
        // Insert right after the left sibling's cell: shift cells right to
        // make room at sibling_index + 1, then write the new key/child.
        uint32_t index = sibling_index + 1;
        for (uint32_t i = original_num_keys; i > index; i--) {
            void* destination = internal_node_cell(parent, i);
            void* source = internal_node_cell(parent, i - 1);
            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
        }
        *internal_node_child(parent, index) = child_page_num;
        *internal_node_key(parent, index) = child_max_key;
        new_child_index = index;
    }
    return new_child_index;
}

/*
 * leaf_node_split_and_insert - Split a full leaf and insert the new row.
 *
 * Allocates a new right leaf. Iterates backwards over all MAX_CELLS + 1
 * logical cells (existing cells plus the one being inserted). For each,
 * decides which node it belongs to (right if index >= LEFT_SPLIT_COUNT,
 * left otherwise) and writes it there. After the loop, both nodes have
 * their cell counts set and their next_leaf pointers linked.
 *
 * Finally, promotes the split: if the old leaf was the root, the tree
 * grows upward via create_new_root (root page number stays fixed). If
 * not, the new sibling is inserted into the already-existing parent.
 */
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* old_node = get_page(cursor->table->pager, cursor->page_num);

    // Allocate the new right sibling.
    uint32_t new_page_num = cursor->table->pager->num_pages;
    void* new_node = get_page(cursor->table->pager, new_page_num);
    initialize_leaf_node(new_node);

    // Link: new_node inherits old_node's next pointer; old_node now points to new_node.
    *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
    *leaf_node_next_leaf(old_node) = new_page_num;

    /*
     * Distribute all MAX_CELLS + 1 cells (old ones + the new row) across
     * the two nodes. Work right-to-left so we can shift without clobbering.
     *
     * For index i in [MAX_CELLS .. 0]:
     *   - Determine destination node and its local index.
     *   - If i == cursor->cell_num, write the new row there.
     *   - Otherwise copy from old_node[i - (i > cursor->cell_num ? 1 : 0)].
     */
    for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--) {
        void* destination_node;
        if ((uint32_t)i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
            destination_node = new_node;
        } else {
            destination_node = old_node;
        }

        uint32_t index_within_node = (uint32_t)i % LEAF_NODE_LEFT_SPLIT_COUNT;
        void* destination = leaf_node_cell(destination_node, index_within_node);

        if ((uint32_t)i == cursor->cell_num) {
            // Slot the new row directly into its sorted position.
            *(uint32_t*)destination = key;
            serialize_row(value, destination + LEAF_NODE_KEY_SIZE);
        } else {
            // Map logical index i back to the original cell in old_node.
            // Logical indices above cursor->cell_num map to old cell i-1
            // (the new row took slot cursor->cell_num, pushing everything up).
            uint32_t old_index = (uint32_t)i > cursor->cell_num
                                 ? (uint32_t)i - 1
                                 : (uint32_t)i;
            memcpy(destination, leaf_node_cell(old_node, old_index),
                   LEAF_NODE_CELL_SIZE);
        }
    }
    *leaf_node_num_cells(old_node) = LEAF_NODE_LEFT_SPLIT_COUNT;
    *leaf_node_num_cells(new_node) = LEAF_NODE_RIGHT_SPLIT_COUNT;

    if (is_node_root(old_node)) {
        create_new_root(cursor->table, new_page_num);
    } else {
        // Old node keeps its parent; the new sibling joins it too.
        uint32_t parent_page_num = *node_parent(old_node);
        *node_parent(new_node) = parent_page_num;

        // Insert new_node into the parent first, using its max key (which
        // still equals old_node's pre-split max) to locate the spot right
        // after old_node. Only afterwards do we touch old_node's separator —
        // fixing it first would corrupt the very search internal_node_insert
        // relies on to find where old_node currently sits.
        void* parent = get_page(cursor->table->pager, parent_page_num);
        uint32_t old_node_new_max_key = get_node_max_key(old_node);
        uint32_t new_child_index = internal_node_insert(cursor->table, parent_page_num, new_page_num);

        // old_node is immediately to the left of the newly inserted child.
        // If that's a keyed cell (not the parent's rightmost), its key is
        // now stale (old_node's max shrank) — fix it.
        if (new_child_index > 0) {
            *internal_node_key(parent, new_child_index - 1) = old_node_new_max_key;
        }
    }
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        leaf_node_split_and_insert(cursor, key, value);
        return;
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

