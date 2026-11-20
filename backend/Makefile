CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -g -Iinclude
AR = ar
ARFLAGS = rcs
LDFLAGS = -lm
BUILD_DIR ?= build

ifeq ($(OS),Windows_NT)
    EXE = .exe
    MKDIR = if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
    RMDIR = if exist "$(subst /,\,$(1))" rmdir /S /Q "$(subst /,\,$(1))"
    RUN = $(subst /,\,$(1))
else
    EXE =
    MKDIR = mkdir -p $(1)
    RMDIR = rm -rf $(1)
    RUN = ./$(1)
endif

LIB = $(BUILD_DIR)/libtransport_core.a

SRCS = src/common.c src/types.c src/linkedlist.c src/stack.c src/queue.c src/tree.c src/heap.c src/avl.c src/hashtable.c
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(SRCS))

TEST_SRCS = tests/test_linkedlist.c tests/test_stack.c tests/test_queue.c tests/test_tree.c tests/test_heap.c tests/test_avl.c tests/test_hashtable.c tests/test_types.c
TEST_BINS = $(patsubst tests/%.c,$(BUILD_DIR)/tests/%$(EXE),$(TEST_SRCS))

.PHONY: all test asan valgrind clean

all: $(LIB)

$(LIB): $(OBJS)
	@$(call MKDIR,$(BUILD_DIR))
	$(AR) $(ARFLAGS) $@ $^

$(BUILD_DIR)/src/%.o: src/%.c
	@$(call MKDIR,$(BUILD_DIR)/src)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%$(EXE): tests/%.c $(LIB)
	@$(call MKDIR,$(BUILD_DIR)/tests)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

test: $(LIB) $(TEST_BINS)
	@echo ========================================
	@echo Running All Test Suites
	@echo ========================================
	@$(call RUN,$(BUILD_DIR)/tests/test_linkedlist$(EXE))
	@$(call RUN,$(BUILD_DIR)/tests/test_stack$(EXE))
	@$(call RUN,$(BUILD_DIR)/tests/test_queue$(EXE))
	@$(call RUN,$(BUILD_DIR)/tests/test_tree$(EXE))
	@$(call RUN,$(BUILD_DIR)/tests/test_heap$(EXE))
	@$(call RUN,$(BUILD_DIR)/tests/test_avl$(EXE))
	@$(call RUN,$(BUILD_DIR)/tests/test_hashtable$(EXE))
	@$(call RUN,$(BUILD_DIR)/tests/test_types$(EXE))
	@echo ========================================
	@echo ALL 8 TEST SUITES PASSED SUCCESSFULLY
	@echo ========================================

asan:
ifeq ($(OS),Windows_NT)
	@echo [ASAN] Notice: AddressSanitizer is not supported under native Windows MinGW GCC.
	@exit 1
else
	$(MAKE) test BUILD_DIR=build_asan CFLAGS="$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
endif

valgrind: test
ifeq ($(OS),Windows_NT)
	@echo [VALGRIND] Notice: Valgrind is not supported natively on Windows (PE binary format).
	@exit 1
else
	@echo ========================================
	@echo Running All Tests under Valgrind
	@echo ========================================
	@for t in $(TEST_BINS); do \
		echo "[VALGRIND] Running $$t ..."; \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$$t || exit 1; \
	done
	@echo ========================================
	@echo ALL 8 TEST SUITES PASSED UNDER VALGRIND
	@echo ========================================
endif

clean:
	@$(call RMDIR,build)
	@$(call RMDIR,build_asan)
