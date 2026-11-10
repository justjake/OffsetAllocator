CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror

.PHONY: check clean

check: regression_tests
	./regression_tests

regression_tests: regression_tests.c offsetAllocator.h
	$(CC) $(CFLAGS) regression_tests.c -o $@

clean:
	rm -f regression_tests
