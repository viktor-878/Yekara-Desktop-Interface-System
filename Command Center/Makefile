
APP_NAME = Command Center
ICON_NAME = command-center
BUNDLE_DIR = $(APP_NAME).deskapp

# Escape spaces for GNU Make target names
empty :=
space := $(empty) $(empty)
APP_NAME_ESC = $(subst $(space),\$(space),$(APP_NAME))
BUNDLE_DIR_ESC = $(APP_NAME_ESC).deskapp

CC = gcc
PKG_CONFIG = pkg-config

CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lXm -lXt -lX11 -lXpm -lyk -larchive -lvterm

# Xft / Fontconfig
XFT_CFLAGS = $(shell $(PKG_CONFIG) --cflags xft fontconfig)
XFT_LIBS = $(shell $(PKG_CONFIG) --libs xft fontconfig)

CFLAGS += $(XFT_CFLAGS)
LDFLAGS += $(XFT_LIBS)

RESOURCES_DIR = resources
GZI_ICON = $(RESOURCES_DIR)/$(ICON_NAME).gzi

SRC = src/main.c
OBJ = $(SRC:.c=.o)

TARGET = $(BUNDLE_DIR_ESC)/$(APP_NAME_ESC)

all: $(GZI_ICON) $(TARGET)

$(TARGET): $(OBJ) $(GZI_ICON)
	mkdir -p "$(BUNDLE_DIR)"

	rm -f "$(BUNDLE_DIR)/app.metadata"
	{ \
		echo "[Application Entry]"; \
		echo "ExecFile=$(APP_NAME);"; \
		echo "Icon=$(ICON_NAME):commandCenter_icon;"; \
		echo ""; \
	} > "$(BUNDLE_DIR)/app.metadata"

	rm -rf "$(BUNDLE_DIR)/$(RESOURCES_DIR)"
	cp -r "$(RESOURCES_DIR)" "$(BUNDLE_DIR)/$(RESOURCES_DIR)"

	$(CC) $(CFLAGS) $(OBJ) -o "$@" $(LDFLAGS)

$(GZI_ICON):
	mkdir -p "$(RESOURCES_DIR)"
	tar -czf "$@" -C icons "$(ICON_NAME)"

install: all
	install -d /applications
	cp -r "$(BUNDLE_DIR)" /applications/

%.o: %.c
	$(CC) $(CFLAGS) -c "$<" -o "$@"

clean:
	rm -rf "$(BUNDLE_DIR)"
	rm -f src/*.o
	rm -f "$(GZI_ICON)"

.PHONY: all install clean
