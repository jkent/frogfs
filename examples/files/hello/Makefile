CFLAGS += -g3 -O0 -Wall -MMD

SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))
DEPS = $(patsubst %.c,%.d,$(OBJS))

main: $(OBJS)
run: main
	@./main
debug: main
	@gdb -ex run --args main

.PHONY: clean run
clean:
	@rm -f *.d *.o history main

-include $(OBJS:%.o=%.d)
