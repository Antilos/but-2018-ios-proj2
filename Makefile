CFLAGS = -std=gnu99 -Wall -Wextra -pedantic -Wno-unknown-pragmas -g3
RESULTS = proj2
ARCHIVE = xkocal00.zip

.PHONY = clean
.PHONY = zip

all: $(RESULTS)

run: $(RESULTS)
	./proj2 6 2 2 2 20 6

runb: proj2b
	./proj2b 6 2 2 2 20 6

proj2: proj2.c
	gcc $(CFLAGS) $^ -lrt -lpthread -o $@

proj2b: proj2Backup.c
	gcc $(CFLAGS) $^ -lrt -lpthread -o $@

#PHONY
clean:
	rm -f $(RESULTS) *.o

zip:
	zip $(ARCHIVE) *.c *.cc *.h Makefile