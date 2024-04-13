#include <stdio.h>
#include "ipt_test.h"

const char* trace_file = "/root/test_syzkaller/trace/trace_data_0";
char trace_data[1000000];

int main() {
	// read trace data from trace_file
	FILE* fp = fopen(trace_file, "rb");
	if (fp == NULL) {
		printf("Error: cannot open file %s\n", trace_file);
		return 1;
	}
	size_t len = fread(trace_data, 1, sizeof(trace_data), fp);
	trace_data[len] = 0x55;
	fclose(fp);
	
	ipt_decoder_t ipt_decoder;
	ipt_decoder.init();

	// decode trace data
	decoder_result_t result = libxdc_decode(ipt_decoder.libxdc, (uint8_t*)trace_data, len);
	if (result == decoder_success) {
		printf("Decoding success\n");
	} else {
		if (result == decoder_page_fault) {
			printf("Decoding failed: page fault in address %lx\n", libxdc_get_page_fault_addr(ipt_decoder.libxdc));
		} else {
			printf("Decoding failed with result %d\n", result);
		}
	}

	return 0;
}