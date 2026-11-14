TARGET := neon_coil
BUILD := build
SOURCES := .
INCLUDES :=

CFLAGS += -O2 -Wall -Wextra -std=gnu99
CFLAGS += -D__PS3__

LIBS += -lSDL -lm

include $(PSL1GHT)/ppu_rules

APPID := NCAR00001
CONTENTID := UP0001-NCAR00001_00-NEONCOILARENA001

PKGDIR := pkg
ICON := $(PKGDIR)/ICON0.PNG

.PHONY: all icon pkg clean

all: $(TARGET).self

icon:
	mkdir -p $(PKGDIR)/USRDIR
	python3 tools/make_icon.py $(ICON)

pkg: $(TARGET).self icon
	cp $(TARGET).self $(PKGDIR)/USRDIR/EBOOT.BIN
	cp sfo.xml $(PKGDIR)/PARAM.SFO.xml

	$(PSL1GHT)/tools/bin/pkg.py \
		--contentid $(CONTENTID) \
		--type 1 \
		--package_version 01.00 \
		--file $(PKGDIR)/USRDIR/EBOOT.BIN=/USRDIR/EBOOT.BIN \
		--file $(ICON)=/ICON0.PNG \
		--output $(TARGET).pkg

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).self $(TARGET).pkg $(PKGDIR)