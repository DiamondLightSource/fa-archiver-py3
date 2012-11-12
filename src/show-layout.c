/* Code for computing the layout of a given structure.  The following macros
 * must be defined before compiling this:
 *  STRUCT_NAME     Name of structure to be analysed
 *  FIELD_LIST      Name of file containing list of fields in structure
 *  FIELD_HEADER    Name of file containing definition to include. */

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "error.h"
#include FIELD_HEADER


#define S_(x)   #x
#define S(x)    S_(x)

struct field {
    const char *name;
    size_t size;
    size_t offset;
};

#define OFFSET(field) \
    { #field, \
      sizeof(((struct STRUCT_NAME *) NULL)->field), \
      offsetof(struct STRUCT_NAME, field) \
    }

const struct field fields[] = {
    #include FIELD_LIST
};

const int field_count = sizeof(fields) / sizeof(fields[0]);

int main(int argc, char **argv)
{
    size_t struct_size = sizeof(struct STRUCT_NAME);
    printf("struct %s: %zu\n", S(STRUCT_NAME), struct_size);

    size_t offset = 0;
    for (int i = 0; i < field_count; i ++)
    {
        const struct field *field = &fields[i];
        if (offset != field->offset)
        {
            printf("    padding: %zu\n", field->offset - offset);
            offset = field->offset;
        }

        printf("%-24s: %3zu / %3zu\n", field->name, field->offset, field->size);
        offset += field->size;
    }
    if (offset != struct_size)
        printf("    padding: %zu\n", struct_size - offset);
}
