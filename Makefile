PREFIX ?= /usr

MAKE=make

all:
	$(MAKE) -C ykclock
	$(MAKE) -C libYekara
	$(MAKE) -C TextBook
	$(MAKE) -C filecabinet

clean:
	$(MAKE) -C ykclock clean
	$(MAKE) -C libYekara clean
	$(MAKE) -C TextBook clean
	$(MAKE) -C filecabinet clean

install:
	./install.sh $(PREFIX)
