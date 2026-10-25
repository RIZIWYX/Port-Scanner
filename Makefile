CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)
TARGET  = scanner

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean