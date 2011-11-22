CC=gcc
CFLAGS=-O3 -c
LDFLAGS=-O3 -lm
SOURCES=MyBot.c rbtree.c list_sort.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=MyBot

all: $(OBJECTS) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ ${OBJECTS}
	$(eval $(resize))

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f ${EXECUTABLE} ${OBJECTS} *.d
tags: 
	ctags -f tags ${SOURCES}

doc: README.rst
	rst2pdf $<

.PHONY: all clean tags doc
