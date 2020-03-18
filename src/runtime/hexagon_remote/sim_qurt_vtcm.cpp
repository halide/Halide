#include "hexagon_standalone.h"
#include <stdlib.h>

bool vtcm_ready = false;
// Represents the base address in VTCM.
const unsigned int TCM_BASE = 0xD800 << 16;
const unsigned int VTCM_BASE_ADDRESS = TCM_BASE + (2 << 20);

typedef struct Node {
    void *addr;
    uint64_t size;
    Node *next;
} Node;

// Free and used memory blocks list.
Node *free_blocks = NULL;
Node *used_blocks = NULL;

// Create a new node with given addr and size.
Node *createNode(void *addr, uint64_t size) {
    assert(size > 0);
    Node *mb = (Node *)malloc(sizeof(Node));
    mb->addr = addr;
    mb->size = size;
    return mb;
}

// Add new node to the given list in ascending order of addr field.
void addNode(Node *&list, Node *a) {
    Node *prev = NULL;
    Node *curr = list;
    while (curr) {
        if ((uint64_t)curr->addr > (uint64_t)a->addr) {
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    if (prev) {
        prev->next = a;
    } else {
        list = a;
    }
    a->next = curr;
}

// Merge adjacent nodes representing continuous address range.
void mergeNodes(Node *&list) {
    if (!list) return;
    Node *prev = list;
    Node *curr = list->next;
    while (curr) {
        if (curr->size == 0) {
            prev->next = curr->next;
            free(curr);
            curr = prev;
        }
        if ((uint64_t)prev->addr + prev->size == (uint64_t)curr->addr) {
            prev->size += curr->size;
            prev->next = curr->next;
            free(curr);
            curr = prev;
        }
        prev = curr;
        curr = curr->next;
    }
}

// Add and merge new node to list in sorted order.
void addAndMerge(Node *&list, Node *a) {
    addNode(list, a);
    mergeNodes(list);
}

// Returns the node with given address and removes from the list.
// Return null if no entry found.
Node *findAndRemove(Node *&list, void *addr) {
    Node *prev = NULL;
    Node *curr = list;
    while (curr) {
        if (curr->addr == addr) {
            if (prev) {
                prev->next = curr->next;
            } else {
                list = list->next;
            }
            curr->next = NULL;
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

// Allocate a node of given size from the list.
void *allocate(Node *list, uint64_t size) {
    if (size <= 0) return NULL;
    while (list) {
        if (list->size >= size) {
            void *addr = list->addr;
            list->addr = (void *)((uint64_t)list->addr + size);
            list->size -= size;
            return addr;
        }
        list = list->next;
    }
    return NULL;
}

// Initialize the VTCM memory to handle HAP_request_VTCM and HAP_release_VTCM
// calls. This function needs to be called only once.
void setup_tcm() {
    uint64_t pa = VTCM_BASE_ADDRESS;
    void *va = (void *)VTCM_BASE_ADDRESS;
    unsigned int xwru = 15;
    // write back and cacheable
    unsigned int cccc = 7;
    unsigned int asid = 0;
    unsigned int aa = 0;
    // Set valid and ignore asid
    unsigned int vg = 3;
    // 256KB VTCM memory on v65.
    unsigned int page_size = 8;
    // Remap Hexagon memory page using direct access to TLB entry.
    add_translation_extended(1, va, pa, page_size, xwru, cccc, asid, aa, vg);
    // Mark all VTCM memory as free.
    Node *mb = createNode((void *)VTCM_BASE_ADDRESS, 1 << (10 + page_size));
    addAndMerge(free_blocks, mb);
    vtcm_ready = true;
}

extern "C" {

// At present we always use single_page_flag = 1 as single_page_flag is
// mandatory for scatter/gather operations as they require to be contained
// within a single page of memory. The size will be aligned to the closest
// possible page size - 4KB / 16KB / 64KB / 256KB
void *HAP_request_VTCM(unsigned int size, unsigned int single_page_flag) {
    // Align size to the closest possible page size - 4KB / 16KB / 64KB / 256KB
    if (single_page_flag == 1) {
        unsigned int size_KB = size >> 10;
        if (size_KB > 64)
            size_KB = 256;
        else if (size_KB > 16)
            size_KB = 64;
        else if (size_KB > 4)
            size_KB = 16;
        else
            size_KB = 4;
        size = size_KB << 10;
    }
    if (!vtcm_ready) setup_tcm();
    // Check if we have enough free memory in VTCM.
    void *addr = allocate(free_blocks, size);
    if (!addr) {
        return NULL;
    }
    // Add entry to used_blocks. Don't merge Nodes here.
    Node *mb = createNode(addr, size);
    addNode(used_blocks, mb);
    return addr;
}

int HAP_release_VTCM(void *pVA) {
    // Remove the entry from the used_blocks list.
    Node *mb = findAndRemove(used_blocks, pVA);
    if (!mb) {
        return -1;
    }
    // Creating nodes in free_blocks gives the opportunity to merge nodes.
    addAndMerge(free_blocks, mb);
    return 0;
}
}
