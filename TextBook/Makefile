APP_NAME = TextBook
ICON_NAME = textbook
BUNDLE_DIR = $(APP_NAME).deskapp

CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lXm -lXt -lX11 -lXpm -lyk -larchive

RESOURCES_DIR = resources
ICON_SRC_DIR = icons/$(ICON_NAME)
GZI_ICON = $(RESOURCES_DIR)/$(ICON_NAME).gzi

SRC = src/main.c
OBJ = $(SRC:.c=.o)

TARGET = $(BUNDLE_DIR)/$(APP_NAME)

all: $(GZI_ICON) $(TARGET)

$(TARGET): $(OBJ)
	mkdir -p $(BUNDLE_DIR)

	rm -f $(BUNDLE_DIR)/app.metadata
	echo "[Application Entry]" >> $(BUNDLE_DIR)/app.metadata
	echo "ExecFile=$(APP_NAME)" >> $(BUNDLE_DIR)/app.metadata
	echo "Icon=$(ICON_NAME)" >> $(BUNDLE_DIR)/app.metadata

	rm -rf $(BUNDLE_DIR)/$(RESOURCES_DIR)
	cp -r $(RESOURCES_DIR) $(BUNDLE_DIR)/$(RESOURCES_DIR)

	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)


# Build the GZI icon
$(GZI_ICON):
	@echo "Packing GZI icon..."
	mkdir -p $(RESOURCES_DIR)
	tar -czf $(GZI_ICON) -C icons $(ICON_NAME)

install:
	$(GZI_ICON) $(TARGET)
	mkdir /applications
	cp -r ./TextBook.deskapp /applications/TextBook.deskapp

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -rf $(BUNDLE_DIR)
	rm -f src/*.o
	rm -f $(GZI_ICON)

.PHONY: all clean
