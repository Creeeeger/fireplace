
#include "bootchain/cpu_internal.h"

void bootchain_cpu_reset(void)
{
	cpu_system_reset();
	cpu_mmu_reset();
	cpu_runtime_reset();
}
