CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -I../common

TARGET  = repactl
SRCS    = repactl.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

clean:
	rm -f $(TARGET) $(OBJS)
