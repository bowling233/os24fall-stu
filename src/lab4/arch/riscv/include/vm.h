#ifndef __VM_H__
#define __VM_H__

void create_mapping(uint64_t *pgtbl, uint64_t va, uint64_t pa, uint64_t sz, uint64_t perm);
void setup_vm_final(void);
uint64_t *sv39_pg_dir_dup(uint64_t *pgtbl);

#endif