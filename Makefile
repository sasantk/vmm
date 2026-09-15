CC      := clang
CFLAGS  := -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g
TARGET  := kvm-sample
SRCS    := $(wildcard *.c)
OBJS    := $(SRCS:.c=.o)

.PHONY: all clean compiledb

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

compiledb:
	@printf '[\n' > compile_commands.json
	@i=0; for f in $(SRCS); do \
		[ $$i -gt 0 ] && printf ',\n' >> compile_commands.json; \
		printf '  {"directory": "%s", "file": "%s", "output": "%s", "arguments": ["%s", %s"-c", "%s", "-o", "%s"]}' \
			"$(CURDIR)" "$$f" "$${f%.c}.o" "$(CC)" \
			"$$(for a in $(CFLAGS); do printf '"%s", ' $$a; done)" \
			"$$f" "$${f%.c}.o" >> compile_commands.json; \
		i=$$((i+1)); \
	done
	@printf '\n]\n' >> compile_commands.json

clean:
	rm -f $(OBJS) $(TARGET) compile_commands.json
