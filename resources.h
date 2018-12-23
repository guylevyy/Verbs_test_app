#ifndef TEST_FUNCTION_H
#define TEST_FUNCTION_H

#include "types.h"

int resource_alloc(struct resources_t *resource);
int resource_init(struct resources_t *resource);
int resource_destroy(struct resources_t *resource);

#endif /* TEST_FUNCTION_H */
