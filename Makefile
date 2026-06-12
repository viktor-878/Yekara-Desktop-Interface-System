PREFIX ?= /usr

MAKE=make

all:
	$(MAKE) -C libYekara
	$(MAKE) -C TextBook

clean:
	$(MAKE) -C libYekara clean
	$(MAKE) -C TextBook clean

install:
	./install.sh $(PREFIX)
