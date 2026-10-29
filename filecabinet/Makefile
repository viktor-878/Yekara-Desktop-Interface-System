# ---------------------------------------------------------------------------
# FileCabinet - Motif file manager, packaged as a .deskapp bundle
#
#   make            build ./File Cabinet.deskapp/
#   make run        build the bundle and run it from inside the bundle
#   make debug      clean rebuild with ASan/UBSan, then bundle
#   make install    copy the bundle to $(APPS_DIR)
#   make clean      remove objects, binary and the bundle
#
# Bundle layout produced:
#   File Cabinet.deskapp/
#     app.metadata      <- copied from ./app.metadata
#     filecabinet       <- the compiled binary (must match ExecFile=)
#     resources/        <- copied from ./resources (all .gzi icons)
#
# Overridable variables (examples):
#   make YK_LIBS="-L/opt/yk/lib -lyk" YK_CFLAGS="-I/opt/yk/include"
#   make APPS_DIR=/usr/local/apps install
# ---------------------------------------------------------------------------

APP_NAME  := File Cabinet
TARGET    := filecabinet

SRC_DIR   := src
# .o/.d files and the raw binary are built next to the sources...
BUILD_DIR := src
# ...and File Cabinet.deskapp is assembled in the project root.
DIST_DIR  := .

APP_METADATA := app.metadata
RES_DIR      := resources

# The bundle name contains a space, so make needs it backslash-escaped in
# targets/prerequisites (BUNDLE), and recipes must quote "$@" and "$<".
empty :=
space := $(empty) $(empty)
BUNDLE    := $(DIST_DIR)/$(subst $(space),\$(space),$(APP_NAME)).deskapp
APPS_DIR  ?= /applications

CC        ?= cc

# libYekara (headers are included as <yk/...>)
YK_CFLAGS ?=
YK_LIBS   ?= -lyk

# Motif / X11
MOTIF_CFLAGS ?=
MOTIF_LIBS   ?= -lXm -lXt -lXpm -lX11

CFLAGS   ?= -O2 -g
CFLAGS   += -std=gnu11 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -I$(SRC_DIR) $(YK_CFLAGS) $(MOTIF_CFLAGS)
LDLIBS   += $(YK_LIBS) $(MOTIF_LIBS)

SRCS      := $(wildcard $(SRC_DIR)/*.c)
OBJS      := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)
RES_FILES := $(shell find $(RES_DIR) -type f 2>/dev/null)
RES_STAMP := $(BUILD_DIR)/resources.stamp

.PHONY: all bundle debug run install clean

all: bundle

# ---- compile / link --------------------------------------------------------

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# -MMD -MP: auto-generate header dependencies
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

# ---- bundle assembly -------------------------------------------------------

.PHONY: icons

icons:
	@for dir in folder cabinet misc; do \
		if [ -d "$$dir" ]; then \
			tar -czf "$(RES_DIR)/$$dir.gzi" -C "$$dir" . && \
			echo "Created $(RES_DIR)/$$dir.gzi"; \
		else \
			echo "Error: $$dir not found" >&2; \
			exit 1; \
		fi; \
	done

bundle: $(BUNDLE)/$(TARGET) $(BUNDLE)/app.metadata $(RES_STAMP)
	@grep -q '^ExecFile=$(TARGET)[[:space:]]*$$' $(APP_METADATA) || \
	  echo "warning: $(APP_METADATA) ExecFile= does not match '$(TARGET)'"
	@echo "Bundle ready: $(DIST_DIR)/$(APP_NAME).deskapp"

$(BUNDLE):
	mkdir -p "$@"

$(BUNDLE)/$(TARGET): $(BUILD_DIR)/$(TARGET) | $(BUNDLE)
	install -m 755 "$<" "$@"

$(BUNDLE)/app.metadata: $(APP_METADATA) | $(BUNDLE)
	install -m 644 "$<" "$@"

# Re-copy resources whenever any file in resources/ changes
$(RES_STAMP): $(RES_FILES) | $(BUNDLE) $(BUILD_DIR)
	rm -rf $(BUNDLE)/resources
	cp -R $(RES_DIR) $(BUNDLE)/resources
	touch $@

# ---- convenience -----------------------------------------------------------

# The app finds its icons via paths relative to the bundle, so run from inside it.
run: bundle
	cd $(BUNDLE) && ./$(TARGET)

debug: CFLAGS := -O0 -g3 -fsanitize=address,undefined -std=gnu11 -Wall -Wextra -Wno-unused-parameter
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean bundle

install: bundle
	mkdir -p "$(APPS_DIR)"
	rm -rf "$(APPS_DIR)/$(APP_NAME).deskapp"
	cp -R $(BUNDLE) "$(APPS_DIR)/"

# NOTE: BUILD_DIR and DIST_DIR point at the source tree / project root, so
# clean must remove only the files we generate - never whole directories.
clean:
	rm -f $(OBJS) $(DEPS) $(BUILD_DIR)/$(TARGET) $(RES_STAMP)
	rm -rf $(BUNDLE)

-include $(DEPS)
