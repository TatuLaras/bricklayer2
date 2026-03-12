NAME = bricklayer
INSTALL_DIR = /usr/local/bin
CC = gcc
BUILD_DIR = build
EBB_COMMON_DIR = ebb_common
SHADER_BUILD_DIR = build/shaders
SRC_DIR = src
SHADER_SRC_DIR = $(SRC_DIR)/shaders
PKG = $(shell pkg-config --libs vulkan glfw3 freetype2) -lm
INCLUDE = -I$(EBB_COMMON_DIR) -I$(EBB_COMMON_DIR)/cglm/include -I$(SRC_DIR) $(shell pkg-config --cflags-only-I freetype2)
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

SHADER_OBJS = $(patsubst $(SHADER_SRC_DIR)/%.slang, $(SHADER_BUILD_DIR)/%.spv, $(wildcard $(SHADER_SRC_DIR)/*.slang))

.PHONY: all
all: $(BUILD_DIR) $(SHADER_BUILD_DIR) $(SHADER_OBJS) $(BUILD_DIR)/$(NAME)

.PHONY: run
run: all
	@echo -e "\n\n\n\n---"
	$(BUILD_DIR)/$(NAME) $(ARGS)

.PHONY: install
install: all
	cp $(BUILD_DIR)/$(NAME) $(INSTALL_DIR)/$(NAME)
	chmod 755 $(INSTALL_DIR)/$(NAME)

OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/*.c)) $(patsubst $(EBB_COMMON_DIR)/%.c, $(BUILD_DIR)/%.o, $(wildcard $(EBB_COMMON_DIR)/*.c)) 

# output executable
$(BUILD_DIR)/$(NAME): $(OBJS)
	gcc $^ -o $@ $(PKG) $(ASAN)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $^ -o $@ $(INCLUDE) $(CFLAGS) $(CC_ARGS)
$(BUILD_DIR)/%.o: $(EBB_COMMON_DIR)/%.c
	$(CC) -c $^ -o $@ $(INCLUDE) $(CFLAGS) $(CC_ARGS)

$(SHADER_BUILD_DIR)/%.spv: $(SHADER_SRC_DIR)/%.slang
	slangc $^ -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
$(SHADER_BUILD_DIR):
	mkdir -p $(SHADER_BUILD_DIR)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

