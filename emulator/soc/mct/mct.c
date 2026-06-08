
#include <stdint.h>
#include <time.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/mct/mct.h>

static struct timespec mct_epoch;

static uint64_t mct_ticks(void)
{
	struct timespec now;
	uint64_t elapsed_ns;

	clock_gettime(CLOCK_MONOTONIC, &now);
	elapsed_ns = (uint64_t)(now.tv_sec - mct_epoch.tv_sec) * 1000000000ULL;
	if (now.tv_nsec >= mct_epoch.tv_nsec)
		elapsed_ns += (uint64_t)(now.tv_nsec - mct_epoch.tv_nsec);
	else
		elapsed_ns -= (uint64_t)(mct_epoch.tv_nsec - now.tv_nsec);

	return elapsed_ns * MCT_FREQUENCY_HZ / 1000000000ULL;
}

int mct_init(struct uc_struct *uc)
{
	clock_gettime(CLOCK_MONOTONIC, &mct_epoch);
	return 0;
}

void mct_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data)
{
	uint64_t ticks;
	uint32_t counter;

	if (type != UC_MEM_READ ||
	    (address != MCT_BASE + MCT_FRC_LOW &&
	     address != MCT_BASE + MCT_FRC_HIGH &&
	     address != MCT_LOCAL_BASE + MCT_LOCAL_TIMER0_CUR_RAW))
		return;

	ticks = mct_ticks();
	counter = address == MCT_BASE + MCT_FRC_HIGH ?
		(uint32_t)(ticks >> 32) :
		(uint32_t)ticks;
	uc_mem_write(uc, address, &counter, sizeof(counter));
}
