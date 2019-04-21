CFLAGS = -std=gnu99 -Wall -Wextra -pedantic -Wno-unknown-pragmas -g3
RESULTS = proj2
ARCHIVE = xkocal00.zip

#define macro DEBUG if you want to show debug messages
DEBUG =

.PHONY = clean
.PHONY = zip

all: $(RESULTS)

run: $(RESULTS)
	./proj2 6 2 2 2 20 6

runb: proj2b
	./proj2b 6 2 2 2 20 6

proj2: proj2.c
	gcc $(CFLAGS) $(DEBUG) $^ -lrt -lpthread -o $@

proj2b: proj2Backup.c
	gcc $(CFLAGS) $^ -lrt -lpthread -o $@

#PHONY
clean:
	rm -f $(RESULTS) *.o

zip:
	zip $(ARCHIVE) *.c *.cc *.h Makefile