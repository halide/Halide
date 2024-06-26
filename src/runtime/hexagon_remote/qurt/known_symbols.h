#ifndef HALIDE_HEXAGON_REMOTE_KNOWN_SYMBOLS_H
#define HALIDE_HEXAGON_REMOTE_KNOWN_SYMBOLS_H

// Mapping between a symbol name and an address.
struct known_symbol {
    const char *name;
    char *addr;
};

// Look up a symbol in an array of known symbols. The map should be
// terminated with a {NULL, NULL} known_symbol.
void *lookup_symbol(const char *sym, const known_symbol *map);

// Look up common symbols.
void *get_known_symbol(const char *sym);

#endif
