PREFIX ?= /usr

MAKE=make

all:
	$(MAKE) -C ykclock
	$(MAKE) -C libYekara
	$(MAKE) -C TextBook

clean:
	$(MAKE) -C ykclock clean
	$(MAKE) -C libYekara clean
	$(MAKE) -C TextBook clean

install:
	./install.sh $(PREFIX)
