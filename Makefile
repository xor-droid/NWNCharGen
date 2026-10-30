CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -g -Wall -Wextra -Wpedantic
LIB_OBJ  = src/cli.o src/util.o src/gff.o src/twoda.o src/tlk.o src/res.o src/rules.o src/chr.o src/levelup.o src/chrgff.o src/look.o src/plan.o src/pick.o src/ini.o src/guide.o src/equip.o
TOOLS    = build/gffdump build/reslist build/bicverify build/bicgen

all: $(TOOLS)

build:
	mkdir -p build

src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c -o $@ $<

build/%: tools/%.c $(LIB_OBJ) | build
	$(CC) $(CFLAGS) -o $@ $< $(LIB_OBJ)

clean:
	rm -rf build src/*.o

check: all
	tests/determinism.sh
	tests/guided.sh
	tests/equip.sh
	tests/prestige.sh

.PHONY: all clean check
