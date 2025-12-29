CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -Wpedantic -Iinclude 
LDFLAGS = -pthread
CFLAGS_DEBUG  = -O0 -g -fsanitize=address -fno-omit-frame-pointer
LDFLAGS_DEBUG = -fsanitize=address

SRCS = src/main.c src/logger.c src/config.c src/resp.c src/kvstore.c src/auth.c src/workqueue.c
OBJS = $(SRCS:.c=.o)

TARGET = repa

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

asan: CFLAGS += $(CFLAGS_DEBUG)
asan: LDFLAGS += $(LDFLAGS_DEBUG)
asan: $(OBJS)
	$(CC) $(OBJS) -o repa-asan $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
