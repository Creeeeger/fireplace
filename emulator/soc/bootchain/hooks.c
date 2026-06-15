
#include <inttypes.h>
#include <stdio.h>

#include "bootchain/bootchain_internal.h"

uc_err bootchain_install_hooks(uc_engine *uc,
			       const struct bootchain_hook *hooks, size_t count)
{
	uc_hook handle;

	for (size_t i = 0; i < count; i++) {
		const struct bootchain_hook *hook = &hooks[i];
		uc_err err = uc_hook_add(uc, &handle, hook->type, hook->callback,
					 hook->user_data, hook->begin, hook->end);

		if (err == UC_ERR_OK)
			continue;
		fprintf(stderr,
			"[Bootchain] failed to install %s hook at 0x%" PRIx64
			"..0x%" PRIx64 ": %s\n",
			hook->name, hook->begin, hook->end, uc_strerror(err));
		return err;
	}
	return UC_ERR_OK;
}

bool bootchain_return_to_link(uc_engine *uc)
{
	uint64_t link;

	return uc_reg_read(uc, UC_ARM64_REG_LR, &link) == UC_ERR_OK &&
	       uc_reg_write(uc, UC_ARM64_REG_PC, &link) == UC_ERR_OK;
}
