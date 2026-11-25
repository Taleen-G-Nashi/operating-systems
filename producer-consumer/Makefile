CC      ?= cc
CFLAGS  ?= -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -pthread

prod_cons: prod_cons.c
	$(CC) $(CFLAGS) prod_cons.c -o prod_cons

# sanitizer builds (used in the notebook / tests)
prod_cons_tsan: prod_cons.c
	$(CC) -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -g -Wall -Wextra -pthread -fsanitize=thread prod_cons.c -o prod_cons_tsan
prod_cons_asan: prod_cons.c
	$(CC) -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -g -Wall -Wextra -pthread -fsanitize=address,undefined prod_cons.c -o prod_cons_asan

test: prod_cons
	bash tests/run_tests.sh ./prod_cons
clean:
	rm -f prod_cons prod_cons_tsan prod_cons_asan
.PHONY: test clean
