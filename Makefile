CC      := clang
CFLAGS  := -std=gnu11 -Wall -Wextra -Wpedantic -g
TARGET  := kvm-sample
SRCS    := $(wildcard *.c)
OBJS    := $(SRCS:.c=.o)
LAST := 100
NUMBERS := $(shell seq 1 ${LAST})

.PHONY: all clean compiledb release debug asan ubsan smoke run100 asan-check ubsan-check check 

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -O2 -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -O2 -c $< -o $@

release:
	mkdir -p bin;
	$(CC) $(CFLAGS) -O2 -o bin/$(TARGET) $(SRCS)

debug:
	mkdir -p bin;
	$(CC) $(CFLAGS) -O0 -o bin/$(TARGET)-$@ $(SRCS)

asan: $(SRCS)
	mkdir -p bin;
	$(CC) $(CFLAGS) -fsanitize=address -fno-omit-frame-pointer -O1 -o bin/$(TARGET)-$@ $^

ubsan: $(SRCS)
	mkdir -p bin;
	$(CC) $(CFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -O1 -o bin/$(TARGET)-$@ $^

smoke: debug
	
	if ! ./bin/kvm-sample-debug >/dev/null ; then\
		echo "failed";\
		exit 1;\
	fi

run100: debug
	for i in $(NUMBERS); do\
		if ! ./bin/kvm-sample-debug >/dev/null ; then\
			echo "failed at run $$i";\
			exit 1;\
		fi;\
	done

asan-check: asan
	if ! ./bin/kvm-sample-asan >/dev/null ; then\
		echo "failed at asan-check";\
		exit 1;\
	fi

ubsan-check: ubsan
	if ! ./bin/kvm-sample-ubsan >/dev/null ; then\
		echo "failed ubsan-check";\
		exit 1;\
	fi

check: smoke run100 asan-check ubsan-check release

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
	rm -rf bin/ $(OBJS) $(TARGET) compile_commands.json
