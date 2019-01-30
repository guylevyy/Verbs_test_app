#ifndef TEST_H
#define TEST_H

int force_configurations_dependencies();
int do_test(struct resources_t *resource);
int init_connection(struct resources_t *resource);
int print_results(struct resources_t *resource);
int sync_configurations(struct resources_t *resource);
int sync_post_connection(struct resources_t *resource);

#endif
