
#include "bootchain/cpu_internal.h"

void bootchain_cpu_reset(bool trace_kernel)
{
	cpu_system_reset();
	cpu_mmu_reset();
	cpu_runtime_reset(trace_kernel);
}
