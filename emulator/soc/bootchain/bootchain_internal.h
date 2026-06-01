
#ifndef FIREPLACE_SOC_BOOTCHAIN_INTERNAL_H
#define FIREPLACE_SOC_BOOTCHAIN_INTERNAL_H

#include <stddef.h>
#include <unicorn/unicorn.h>

uc_err bootchain_read_profile_file(const char *filename, char **data,
				   size_t *size);

#endif
