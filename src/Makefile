# Run from repo root: make -C src organize-includes
# Or from : make organize-includes

SOURCES := $(shell find . -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.hpp' -o -name '*.h' \) | sort)

.PHONY: format
format:
	organize-includes -n -I $(CURDIR) $(SOURCES)

bulk.hpp:
	grep -r -h '^#include <' | sort -u | grep -v bas/ >$@

bulk.h: bulk.hpp
	grep -v '.hpp' $< >$@

