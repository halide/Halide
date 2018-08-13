#include <hexagon_standalone.h>
#include "log.h"
#include <stdlib.h>

bool vtcm_ready = false;
const unsigned int TCM_BASE = 0xD800 << 16;
const unsigned int VTCM_BASE_ADDRESS = TCM_BASE + (2 << 20);

typedef struct Node{
    void* addr;
    uint64_t size;
    Node* next;
} Node;

Node* free_blocks = NULL;
Node* used_blocks = NULL;

Node* createNode(void* addr, uint64_t size) {
    Node* mb = (Node*)malloc(sizeof(Node));
    mb->addr = addr;
    mb->size = size;
    return mb;
}

void addNode(Node* &list, Node* a) {
    Node* prev = NULL;
    Node* curr = list;
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

// 
void mergeNodes(Node* &list) {
    if (!list) return;
    Node* prev = list;
    Node* curr = list->next;
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
void addAndMerge(Node* &list, Node* a) {
    addNode(list, a);
    mergeNodes(list);
}

Node* findAndRemove(Node* &list, void* addr) {
    Node* prev = NULL;
    Node* curr = list;
    while (curr) {
        if (list->addr == addr) {
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

void* allocate(Node* list, uint64_t size) {
    if (size <= 0) return NULL;
    while(list) {
        if (list->size >= size) {
            void* addr = list->addr;
            list->addr = (void *) ((uint64_t) list->addr + size);
            list->size -= size;
            return addr;
        }
        list = list->next;
    }
    return NULL;
}

void setup_tcm() {
    uint64_t pa = VTCM_BASE_ADDRESS;
    void* va = (void *)VTCM_BASE_ADDRESS;
    unsigned int xwru = 15;
    unsigned int cccc = 7; // write back and cacheable
    unsigned int asid = 0;
    unsigned int aa = 0;
    unsigned int vg = 3; // Set valid and ignore asid
    unsigned int page_size = 8; // 256KB
    add_translation_extended(1, va, pa, page_size, xwru, cccc, asid, aa, vg);
    Node* mb = createNode((void *)VTCM_BASE_ADDRESS, 1 << (10 + page_size));
    addAndMerge(free_blocks, mb);
    log_printf("Adding 256KB VTCM Page at VA:%x PA:%llx\n", (int)va, pa);
    vtcm_ready =  true;
}

extern "C" {

void* HAP_request_VTCM(unsigned int size, unsigned int single_page_flag) {
    if (!vtcm_ready) setup_tcm();
    void* addr = allocate(free_blocks, size);
    if (!addr) {
        log_printf("HAP_request_VTCM returned NULL\n", addr);
        return NULL;
    }
    Node* mb = createNode(addr, size);
    addNode(used_blocks, mb);
    return addr;
}

int HAP_release_VTCM(void* pVA) {
    Node* mb = findAndRemove(used_blocks, pVA);
    if (!mb) {
        log_printf("HAP_release_VTCM called on an invalid address %x\n", pVA);
        return -1;
    }
    addAndMerge(free_blocks, mb);
    return 0;
}

}
