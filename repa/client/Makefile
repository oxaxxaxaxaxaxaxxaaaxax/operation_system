CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -pedantic

TARGET  = repactl
SRCS    = repactl.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

clean:
	rm -f $(TARGET) $(OBJS)
