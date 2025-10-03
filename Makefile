# My typical Makefile format, but without an include directory

PROJECT_NAME=wm
SRC_DIR=src
#INC_DIR=include
OBJ_DIR=obj
BIN_DIR=bin

#CFLAGS = -Wall -Wextra -Wpedantic -Werror -std=c11 -ggdb -I$(INC_DIR)
CFLAGS = -Wall -Wextra -Wpedantic -Werror -std=c11 -ggdb
CFLAGS += -Wno-unused
LDFLAGS = -lxcb -lxkbcommon

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(BIN_DIR)/$(PROJECT_NAME): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(OBJ_DIR):
	mkdir -p $@
$(BIN_DIR):
	mkdir -p $@

.PHONY: clean build test

build: $(BIN_DIR)/$(PROJECT_NAME)

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(BIN_DIR)

test: build
	Xephyr -br -ac -noreset -screen 800x400 :2&
	@sleep 1
	DISPLAY=:2 ./$(BIN_DIR)/$(PROJECT_NAME);pkill Xephyr
