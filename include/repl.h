#ifndef NOPALDB_REPL_H
#define NOPALDB_REPL_H

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    char   *buffer;
    size_t  buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

InputBuffer      *new_input_buffer(void);
void              print_prompt(void);
void              read_input(InputBuffer *input_buffer);
MetaCommandResult do_meta_command(InputBuffer *input_buffer);
void              repl_start(void);

#endif /* NOPALDB_REPL_H */
