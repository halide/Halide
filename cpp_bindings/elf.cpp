#include "elf.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void saveELF(const char *filename, void *buf, size_t len) {
    FILE *f = fopen(filename, "w");
  
  
    struct elf64_hdr {
        uint8_t	ident[16];	/* ELF "magic number" */
        uint16_t type;
        uint16_t machine;
        uint32_t version;
        uint64_t entry;		/* Entry point virtual address */
        uint64_t phoff;		/* Program header table file offset */
        uint64_t shoff;		/* Section header table file offset */
        uint32_t flags;
        uint16_t ehsize;
        uint16_t phentsize;
        uint16_t phnum;
        uint16_t shentsize;
        uint16_t shnum;
        uint16_t shstrndx;
    } header;
  
    // N.B 17 chars long. Normal string algos will not work.
    const char *strtab_contents = "\0.shstrtab\0.text\0";
  
    memset(header.ident, 0, 16);
    header.ident[0] = 0x7f;
    header.ident[1] = 'E';
    header.ident[2] = 'L';
    header.ident[3] = 'F';
    header.ident[4] = 2;
    header.ident[5] = 1;
    header.ident[6] = 1;
    header.type = 1;
    header.machine = 62;
    header.version = 1;
    header.entry = 0;
    header.phoff = 0;
    header.shoff = 64 + len + 17;
    header.flags = 0;
    header.ehsize = 64;
    header.phentsize = 0;
    header.phnum = 0;
    header.shentsize = 64;
    header.shnum = 3;
    header.shstrndx = 1;
  
    struct elf64_shdr {
        uint32_t name;		/* Section name, index in string tbl */
        uint32_t type;		/* Type of section */
        uint64_t flags;		/* Miscellaneous section attributes */
        uint64_t addr;		/* Section virtual addr at execution */
        uint64_t offset;	/* Section file offset */
        uint64_t size;		/* Size of section in bytes */
        uint32_t link;		/* Index of another section */
        uint32_t info;		/* Additional section information */
        uint64_t addralign;	/* Section alignment */
        uint64_t entsize;	/* Entry size if section holds table */
    } zero, strtab, code;
  
    memset(&zero, 0, sizeof(zero));

    strtab.name = 1;
    strtab.type = 3;
    strtab.flags = 0;
    strtab.addr = 0;
    strtab.offset = sizeof(header) + len;
    strtab.size = 17;
    strtab.link = 0;
    strtab.info = 0;
    strtab.addralign = 1;
    strtab.entsize = 0;

    code.name = 11; 
    code.type = 1;
    code.flags = 6;
    code.addr = 0;
    code.offset = sizeof(header);
    code.size = len;
    code.link = 0;
    code.info = 0;
    code.addralign = 4;
    code.entsize = 0;

    fwrite(&header, 1, 64, f);
    fwrite(buf, 1, len, f);
    fwrite(strtab_contents, 1, 17, f);
    fwrite(&zero, 1, 64, f);
    fwrite(&strtab, 1, 64, f);
    fwrite(&code, 1, 64, f);
    fclose(f);
}
