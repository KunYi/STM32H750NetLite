#include <stddef.h>

extern void Bootloader_DynamicAllocationIsForbidden(void);

void *__wrap_malloc(size_t size)
{
    (void)size;
    Bootloader_DynamicAllocationIsForbidden();
    return NULL;
}

void *__wrap_calloc(size_t count, size_t size)
{
    (void)count;
    (void)size;
    Bootloader_DynamicAllocationIsForbidden();
    return NULL;
}

void *__wrap_realloc(void *ptr, size_t size)
{
    (void)ptr;
    (void)size;
    Bootloader_DynamicAllocationIsForbidden();
    return NULL;
}

void __wrap_free(void *ptr)
{
    (void)ptr;
    Bootloader_DynamicAllocationIsForbidden();
}
