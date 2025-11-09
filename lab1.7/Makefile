# fixed by spec
CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -fsanitize=address -fsanitize=leak -g
CPPFLAGS= -Iinclude

LIBSO   = libuthreads.so
TARGET  = uthreads_example

# чтобы бинарник находил .so рядом с собой
LDFLAGS = -Wl,-rpath,'$$ORIGIN'

all: $(LIBSO) $(TARGET)

# сборка динамической библиотеки (.so): добавляем -fPIC и -shared только здесь
$(LIBSO): src/uthreads.c include/uthreads.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -shared src/uthreads.c -o $@

# сборка примера и линковка с нашей .so
$(TARGET): tests/main.c $(LIBSO)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/main.c -L. -luthreads $(LDFLAGS) -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(LIBSO)
