.POSIX:
PREFIX = /usr/local
LIB = -lwayland-client
BIN = wlclipserv
OBJ = protocol/wlr-data-control-unstable-v1.o

all: $(BIN)

wlclipserv: wlclipserv.o $(OBJ)
	${CC} -g wlclipserv.o $(OBJ) $(LIB) -o $@

wlclipserv.o: wlclipserv.c protocol/wlr-data-control-unstable-v1-client-protocol.h

protocol/wlr-data-control-unstable-v1.c: protocol/wlr-data-control-unstable-v1.xml
	wayland-scanner private-code protocol/wlr-data-control-unstable-v1.xml $@

protocol/wlr-data-control-unstable-v1-client-protocol.h: protocol/wlr-data-control-unstable-v1.xml
	wayland-scanner client-header protocol/wlr-data-control-unstable-v1.xml $@

.c.o:
	$(CC) -Wall -Wpedantic $(CFLAGS) -c $< -o $@

install:
	install -Dm755 -t $(DESTDIR)$(PREFIX)/bin $(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wlclipserv

clean:
	rm -f *.o $(BIN) protocol/*.[och]

