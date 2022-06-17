CC=gcc

ifeq "$(type)" "mthread"
	CFLAGS=-ggdb3 -c -Wall -std=gnu99 -DMTHREAD
else 
	CFLAGS=-ggdb3 -c -Wall -std=gnu99
endif


LDFLAGS=-pthread
SOURCES=httpserver.c libhttp.c wq.c mthread.c mprocess.c lq.c utils.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=httpserver


all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(EXECUTABLE) $(OBJECTS)

install: all FORCE
	cp httpserver /usr/local/bin/
	cp ./httpserver.conf /etc/httpserver.conf

uninstall: FORCE
	rm -rf /usr/local/bin/httpserver
	rm -rf /etc/httpserver.conf
FORCE:

