/*
 * Decompress one Oodle block with a Windows Oodle library (oo2core_*_win64.dll) on Linux.
 *
 * The pak files of the game use compression method 15 (Oodle). The game image has the Oodle
 * code linked in, and there is no Linux library. This tool maps the DLL image, applies the
 * relocations, connects the imports to local stubs, and calls OodleLZ_Decompress. It does not
 * run the DLL entry point. Only memory functions have real stubs.
 *
 * Build:  gcc -O2 -o oodle_dec oodle_loader.c
 * Usage:  oodle_dec DLL RAW_SIZE < compressed > plain
 */
#define _GNU_SOURCE
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef ARCH_SET_GS
#define ARCH_SET_GS 0x1001
#endif

#define MS __attribute__((ms_abi))

static MS void *stub_heap_alloc(void *heap, uint32_t flags, size_t size) {
    (void)heap;
    return (flags & 8) ? calloc(1, size) : malloc(size);
}
static MS int stub_heap_free(void *heap, uint32_t flags, void *block) {
    (void)heap; (void)flags;
    free(block);
    return 1;
}
static MS void *stub_heap_realloc(void *heap, uint32_t flags, void *block, size_t size) {
    (void)heap; (void)flags;
    return realloc(block, size);
}
static MS size_t stub_heap_size(void *heap, uint32_t flags, void *block) {
    (void)heap; (void)flags;
    return malloc_usable_size(block);
}
static MS void *stub_process_heap(void) { return (void *)1; }
static MS void *stub_identity(void *value) { return value; }
static MS int stub_true(void) { return 1; }
static MS uint64_t stub_zero(void) { return 0; }

struct import_stub { const char *name; void *function; };
static const struct import_stub STUBS[] = {
    {"HeapAlloc", stub_heap_alloc},   {"HeapFree", stub_heap_free},
    {"HeapReAlloc", stub_heap_realloc}, {"HeapSize", stub_heap_size},
    {"GetProcessHeap", stub_process_heap}, {"EncodePointer", stub_identity},
    {"DecodePointer", stub_identity}, {"IsProcessorFeaturePresent", stub_true},
    {"InitializeCriticalSectionAndSpinCount", stub_true},
};

static uint8_t *read_all(FILE *file, size_t *size) {
    size_t capacity = 1 << 20, used = 0;
    uint8_t *data = malloc(capacity);
    for (;;) {
        size_t count = fread(data + used, 1, capacity - used, file);
        used += count;
        if (count == 0) break;
        if (used == capacity) data = realloc(data, capacity *= 2);
    }
    *size = used;
    return data;
}

static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint64_t u64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: oodle_dec DLL RAW_SIZE < compressed > plain\n");
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file) { perror("dll"); return 1; }
    size_t file_size;
    uint8_t *raw = read_all(file, &file_size);
    fclose(file);

    const uint8_t *pe = raw + u32(raw + 0x3C);
    uint16_t section_count = u16(pe + 6);
    const uint8_t *optional = pe + 24;
    uint64_t preferred = u64(optional + 24);
    uint32_t image_size = u32(optional + 56), header_size = u32(optional + 60);
    const uint8_t *directories = optional + 112;
    uint8_t *image = mmap(NULL, image_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image == MAP_FAILED) { perror("mmap"); return 1; }
    memcpy(image, raw, header_size);
    const uint8_t *section = optional + u16(pe + 20);
    for (int index = 0; index < section_count; ++index, section += 40) {
        uint32_t address = u32(section + 12), raw_size = u32(section + 16), pointer = u32(section + 20);
        if (raw_size) memcpy(image + address, raw + pointer, raw_size);
    }
    /* Base relocations (directory 5). */
    uint32_t reloc = u32(directories + 5 * 8), reloc_size = u32(directories + 5 * 8 + 4);
    int64_t delta = (int64_t)(uintptr_t)image - (int64_t)preferred;
    for (uint32_t at = 0; at + 8 <= reloc_size;) {
        uint32_t page = u32(image + reloc + at), block = u32(image + reloc + at + 4);
        if (block < 8) break;
        for (uint32_t entry = 8; entry + 2 <= block; entry += 2) {
            uint16_t item = u16(image + reloc + at + entry);
            if ((item >> 12) == 10) {
                uint64_t value = u64(image + page + (item & 0xFFF)) + (uint64_t)delta;
                memcpy(image + page + (item & 0xFFF), &value, 8);
            }
        }
        at += block;
    }
    /* Imports (directory 1). */
    for (uint8_t *entry = image + u32(directories + 8); u32(entry + 12); entry += 20) {
        uint32_t lookup = u32(entry) ? u32(entry) : u32(entry + 16);
        uint64_t *thunk = (uint64_t *)(image + u32(entry + 16));
        for (int index = 0;; ++index) {
            uint64_t item = u64(image + lookup + 8 * (size_t)index);
            if (!item) break;
            void *function = stub_zero;
            if (!(item >> 63)) {
                const char *name = (const char *)(image + item + 2);
                for (size_t stub = 0; stub < sizeof(STUBS) / sizeof(STUBS[0]); ++stub)
                    if (!strcmp(name, STUBS[stub].name)) function = STUBS[stub].function;
            }
            thunk[index] = (uint64_t)(uintptr_t)function;
        }
    }
    /* Export OodleLZ_Decompress (directory 0). */
    const uint8_t *exports = image + u32(directories);
    uint32_t names = u32(exports + 24);
    void *decompress = NULL;
    for (uint32_t index = 0; index < names; ++index) {
        const char *name = (const char *)(image + u32(image + u32(exports + 32) + 4 * index));
        if (!strcmp(name, "OodleLZ_Decompress")) {
            uint16_t ordinal = u16(image + u32(exports + 36) + 2 * index);
            decompress = image + u32(image + u32(exports + 28) + 4 * ordinal);
        }
    }
    if (!decompress) { fprintf(stderr, "OodleLZ_Decompress not found\n"); return 1; }

    /*
     * Windows code reads the thread block through the GS segment (stack probes read the stack
     * limit at gs:[0x10]). Linux does not use GS in user code. Give it a zero block: a stack
     * limit of zero stops all probes.
     */
    uint64_t *thread_block = calloc(1, 0x2000);
    thread_block[0x30 / 8] = (uint64_t)(uintptr_t)thread_block;
    thread_block[0x58 / 8] = (uint64_t)(uintptr_t)(thread_block + 0x200);
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, thread_block) != 0) { perror("arch_prctl"); return 1; }

    size_t packed_size;
    uint8_t *packed = read_all(stdin, &packed_size);
    intptr_t plain_size = (intptr_t)strtoull(argv[2], NULL, 0);
    uint8_t *plain = malloc((size_t)plain_size + 64);
    typedef MS intptr_t (*decompress_fn)(const void *, intptr_t, void *, intptr_t, int, int, int,
                                         void *, intptr_t, void *, void *, void *, intptr_t, int);
    intptr_t result = ((decompress_fn)decompress)(packed, (intptr_t)packed_size, plain, plain_size,
                                                  1, 0, 0, NULL, 0, NULL, NULL, NULL, 0, 3);
    if (result != plain_size) {
        fprintf(stderr, "decompress result %ld, expected %ld\n", (long)result, (long)plain_size);
        return 1;
    }
    fwrite(plain, 1, (size_t)plain_size, stdout);
    return 0;
}
