
APP_NAME = TextBook
ICON_NAME = textbook
BUNDLE_DIR = $(APP_NAME).deskapp
FILETYPES_NAME = files

CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lXm -lXt -lX11 -lXpm -lyk -larchive

RESOURCES_DIR = resources
GZI_ICON = $(RESOURCES_DIR)/$(ICON_NAME).gzi
FILETYPES_ICON = $(RESOURCES_DIR)/$(FILETYPES_NAME).gzi

SRC = src/main.c
OBJ = $(SRC:.c=.o)

TARGET = $(BUNDLE_DIR)/$(APP_NAME)

all: $(GZI_ICON) $(FILETYPES_ICON) $(TARGET)

$(TARGET): $(OBJ) $(GZI_ICON) $(FILETYPES_ICON)
	mkdir -p $(BUNDLE_DIR)

	rm -f $(BUNDLE_DIR)/app.metadata
	{ \
		echo "[Application Entry]"; \
		echo "ExecFile=$(APP_NAME)"; \
		echo "Icon=$(ICON_NAME):textbook_xpm"; \
		echo ""; \
		echo "[text Entry]"; \
		echo "Icon=$(FILETYPES_NAME):text"; \
		echo "Handler=open"; \
		echo ""; \
		echo "[txt Entry]"; \
		echo "Icon=$(FILETYPES_NAME):text"; \
		echo "Handler=open"; \
		echo ""; \
		echo "[c Entry]"; \
		echo "Icon=$(FILETYPES_NAME):text"; \
		echo "Handler=open"; \
		echo ""; \
		echo "[h Entry]"; \
		echo "Icon=$(FILETYPES_NAME):text"; \
		echo "Handler=open"; \
		echo ""; \
		echo "[metadata Entry]"; \
		echo "Icon=$(FILETYPES_NAME):text"; \
		echo "Handler=open"; \
		echo ""; \
		echo "[meta Entry]"; \
		echo "Icon=$(FILETYPES_NAME):text"; \
		echo "Handler=open"; \
	} > $(BUNDLE_DIR)/app.metadata

	rm -rf $(BUNDLE_DIR)/$(RESOURCES_DIR)
	cp -r $(RESOURCES_DIR) $(BUNDLE_DIR)/$(RESOURCES_DIR)

	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

$(GZI_ICON):
	mkdir -p $(RESOURCES_DIR)
	tar -czf $@ -C icons $(ICON_NAME)

$(FILETYPES_ICON):
	mkdir -p $(RESOURCES_DIR)
	tar -czf $@ -C icons $(FILETYPES_NAME)

install: all
	install -d /applications
	cp -r $(BUNDLE_DIR) /applications/

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUNDLE_DIR)
	rm -f src/*.o
	rm -f $(GZI_ICON) $(FILETYPES_ICON)

.PHONY: all install clean
