CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
DEPFLAGS = -MMD -MP

SRC_DIR = src
OBJ_DIR = obj
TARGET  = scanner

SRC  = $(wildcard $(SRC_DIR)/*.c)
OBJ  = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS = $(OBJ:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

re: clean all

-include $(DEPS)

.PHONY: all clean re