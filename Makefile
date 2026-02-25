NAME = bricklayer
CC = gcc
BUILD_DIR = build
SRC_DIR = src
PKG = $(shell pkg-config --libs vulkan glfw3)
INCLUDE = 
ARGS =
DEBUG_FLAGS = -DNDEBUG

ifeq ($(SANITIZE),yes)
ASAN = -fsanitize=address
DEBUG_FLAGS = -ggdb -DDEBUG
endif

ifeq ($(DEBUG),yes)
DEBUG_FLAGS = -ggdb -DDEBUG
endif

CFLAGS = -Wall -Wextra -Wshadow -pedantic -Wstrict-prototypes -march=native $(ASAN) $(DEBUG_FLAGS)

.PHONY: all
all: $(BUILD_DIR) $(BUILD_DIR)/$(NAME)

.PHONY: run
run: all
	@echo -e "\n\n\n\n---"
	@$(BUILD_DIR)/$(NAME) $(ARGS)

OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/*.c))

# output executable
$(BUILD_DIR)/$(NAME): $(OBJS)
	gcc $^ -o $@ $(PKG) $(ASAN)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $^ -o $@ $(INCLUDE) $(CFLAGS) $(CC_ARGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

.PHONY: clean
clean:
	rm $(BUILD_DIR)/*

