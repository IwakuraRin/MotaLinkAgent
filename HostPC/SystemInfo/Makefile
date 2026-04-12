# SystemInfo — GTK3, Ubuntu 20.04 LTS
PKG = gtk+-3.0
CFLAGS += -Wall -Wextra -O2 -std=c11 $(shell pkg-config --cflags $(PKG))
LDFLAGS += $(shell pkg-config --libs $(PKG))

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
ICONDIR = $(DATADIR)/icons/hicolor/scalable/apps
APPSDIR = $(DATADIR)/applications

TARGET = systeminfo
SRC = src/main.c
PACK_NAME = SystemInfo-portable

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(ICONDIR)
	install -m 644 data/systeminfo.svg $(DESTDIR)$(ICONDIR)/systeminfo.svg
	install -d $(DESTDIR)$(APPSDIR)
	install -m 644 data/com.omniroam.systeminfo.desktop $(DESTDIR)$(APPSDIR)/
	-gtk-update-icon-cache -f "$(DESTDIR)$(DATADIR)/icons/hicolor" 2>/dev/null

install-user: $(TARGET)
	sh "$(CURDIR)/pack/install-user.sh"

# Tarball: binary + icon + .desktop + install script (for another machine / backup).
pack: $(TARGET)
	rm -rf "$(PACK_NAME)" "$(PACK_NAME).tar.gz"
	mkdir -p "$(PACK_NAME)/data"
	cp "$(TARGET)" "$(PACK_NAME)/"
	cp data/systeminfo.svg data/com.omniroam.systeminfo.desktop "$(PACK_NAME)/data/"
	cp pack/install-user.sh "$(PACK_NAME)/install-user.sh"
	chmod 755 "$(PACK_NAME)/install-user.sh" "$(PACK_NAME)/$(TARGET)"
	tar -czvf "$(PACK_NAME).tar.gz" "$(PACK_NAME)"
	rm -rf "$(PACK_NAME)"

clean:
	rm -f $(TARGET) "$(PACK_NAME).tar.gz"

.PHONY: all install install-user pack clean
