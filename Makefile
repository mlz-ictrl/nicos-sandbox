DESTDIR =
PREFIX = /usr/local

EXEC = nicos-sandbox-helper
CFLAGS = -O2 -Wall -g
LDFLAGS = -g

all: $(EXEC)

OBJS = nicos-sandbox-helper.o

$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f *.o $(EXEC)

install: $(EXEC)
	install -o root -m 4755 -T $(EXEC) $(DESTDIR)$(PREFIX)/bin/$(EXEC)
