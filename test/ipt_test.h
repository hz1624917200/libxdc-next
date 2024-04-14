#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/time.h>
extern "C" {
#include <libxdc.h>
}

#define SYZIPT_MMAP_PAGES 512 * 1024
#define SYZIPT_MMAP_BASE 0xffffffff80000000
#define perfIntelPtPerfType 8
#define _PERF_EVENT_SIZE (1024 * 512)
#define _PERF_AUX_SIZE (512 * 1024)
#define FILTERS                                       \
	{                                             \
		{0x1000, 0xffffffffff000000}, {0, 0}, {0, 0}, {0, 0} \
	}

typedef void* (*fetch_memory_page_t)(uint64_t, uint8_t*);

struct ipt_driver_t {
	int driver_fd;
	uint8_t* memory_buf;

	ipt_driver_t()
	{
		driver_fd = open("/proc/syzipt", O_RDONLY);
		if (driver_fd < 0) {
			printf("Error: cannot open /proc/syzipt\n");
			exit(1);
		}
		memory_buf = (uint8_t*)mmap(NULL, SYZIPT_MMAP_PAGES * 4096ul, PROT_READ, MAP_PRIVATE, driver_fd, 0);
	}
};

ipt_driver_t ipt_driver;

void* fetch_memory_page(uint64_t addr, uint8_t* driver_buf)
{
	return (void*)(driver_buf + (addr - SYZIPT_MMAP_BASE));
}

void* page_fetch_callback(void* memory_reader_func, uint64_t addr, bool* ret)
{
	addr &= 0xfffffffffffff000;
	// printf("Info: page base addr 0x%lx\n", addr);
	fetch_memory_page_t fetch_memory_page_func = (fetch_memory_page_t)memory_reader_func;
	*ret = true;
	// TODO: return false if failed
	return fetch_memory_page_func(addr, ipt_driver.memory_buf);
}

libxdc_config_t libxdc_cfg = {
    .filter = FILTERS,
    .page_cache_fetch_fptr = &page_fetch_callback,
    .page_cache_fetch_opaque = (void*)fetch_memory_page,
    .bitmap_ptr = NULL,
    .bitmap_size = 0x10000,
    .cov_ptr = NULL,
    .cov_size = 0x100000,
    .align_psb = false,
};

struct ipt_decoder_t {
	libxdc_t* libxdc;
	uint32_t* cov_data;
	uint8_t* trace_input;

	void init()
	{
		// mmap signal
		cov_data = (uint32_t*)mmap(NULL, libxdc_cfg.cov_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		libxdc_cfg.cov_ptr = cov_data;
		libxdc = libxdc_init(&libxdc_cfg);
		if (libxdc == NULL) {
			printf("libxdc init failed", "cov data: %p", cov_data);
			exit(-1);
		}
		trace_input = (uint8_t*)mmap(NULL, _PERF_AUX_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}

	void decode(uint64_t size)
	{
		printf("Decoding trace..., size: 0x%lx\n", size);
		struct timeval start, end;
		gettimeofday(&start, NULL);
		decoder_result_t res = libxdc_decode(libxdc, trace_input, size);
		gettimeofday(&end, NULL);
		unsigned long time = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
		if (res == decoder_success) {
			printf("Decode success! Time taken in usec: %lu\n", time);
		} else {
			printf("Decode failed with result: %d\n", res);
			if (res == decoder_page_fault) {
				printf("Page fault addr: 0x%lx\n", libxdc_get_page_fault_addr(libxdc));
			}
		}
	}

	inline uint32_t get_cov(uint32_t index)
	{
		return cov_data[index + 1];
	}

	void reset_signal()
	{
		*(uint32_t*)cov_data = 0;
	}

	uint32_t get_cov_count()
	{
		return *(uint32_t*)cov_data;
	}
};