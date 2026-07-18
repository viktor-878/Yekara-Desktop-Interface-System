CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -fPIC -larchive -lXm -lXt -lX11 -DYK_DEBUG_RENDERTABLE
LDFLAGS =

PREFIX ?= /usr

# --- library ---
LIB_NAME = libyk.so

SRC_DIR = src
INC_DIR = include

LIB_SRC = $(SRC_DIR)/bundle.c $(SRC_DIR)/gzi.c $(SRC_DIR)/style.c
LIB_OBJ = $(LIB_SRC:.c=.o)

# --- runner ---
RUNNER_SRC = runner/runner.c
RUNNER_OBJ = $(RUNNER_SRC:.c=.o)
RUNNER_BIN = yk-runner

# --- build ---
all: $(LIB_NAME) $(RUNNER_BIN)

$(LIB_NAME): $(LIB_OBJ)
	$(CC) -shared -o $@ $^

$(RUNNER_BIN): $(RUNNER_OBJ) $(LIB_NAME)
	$(CC) -o $@ $(RUNNER_OBJ) -L. -lyk -larchive -lXm -lXt -lX11

# --- generic compile rule ---
%.o: %.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# --- cleanup ---
clean:
	rm -f $(LIB_OBJ) $(RUNNER_OBJ) $(LIB_NAME) $(RUNNER_BIN)

# --- install ---
install:
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/include/yk

	cp $(LIB_NAME) $(DESTDIR)$(PREFIX)/lib
	cp $(RUNNER_BIN) $(DESTDIR)$(PREFIX)/bin
	cp $(INC_DIR)/yk/*.h $(DESTDIR)$(PREFIX)/include/yk

# --- dev helper ---
run: all
	LD_LIBRARY_PATH=. ./$(RUNNER_BIN)

.PHONY: all clean install run
