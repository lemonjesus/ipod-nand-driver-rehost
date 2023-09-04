#define _USE_LARGEFILE64
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>
#include <signal.h>

#include "file-cow.h"

#define ENABLE_ACCESS_LOGGING 1
#define LOG_ACCESS if(ENABLE_ACCESS_LOGGING) printf

static void* progmem;
static void* staticmem;
static uint32_t NAND_IDs[8] = {0xa5d5d589, 0xa5d5d589, 0xa5d5d589, 0xa5d5d589, 0, 0, 0, 0};

#define BANK_COUNT 4
#define PAGE_SIZE 2048

cow_file* nand_bank[4];
cow_file* nand_spare[4];

void* allocate_fixed(uint32_t addr, int size) {
    void* ret = mmap((void*)addr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (ret == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    return ret;
}

void* ipod_malloc(void* trash, int size) {
    return calloc(1, size);
}

void patch_function(uint32_t offset, void* function) {
    ((uint16_t*) (progmem + offset + 0))[0] = 0xb510; // push {r4, lr}
    ((uint16_t*) (progmem + offset + 2))[0] = 0x4678; // mov r0, pc
    ((uint16_t*) (progmem + offset + 4))[0] = 0x6840; // ldr r0, [r0, 4]
    ((uint16_t*) (progmem + offset + 6))[0] = 0x4780; // blx r0
    ((uint16_t*) (progmem + offset + 8))[0] = 0xbd10; // pop {r4, pc}
    ((uint32_t*) (progmem + offset + 10))[0] = function;
}

uint32_t is_page_all_FFs(uint8_t* data_buf) {
    for (int i = 0; i < 16; i++) {
        if (data_buf[i] != 0xff) {
            return 0;
        }
    }
    return 1;
}

uint32_t FIL_readIds() {
    // populate the expected arrays with the NAND IDs
    ((uint32_t*)(staticmem + 0x588))[0] = NAND_IDs;
    return 0;
}

uint32_t FIL_readSinglePage(uint16_t bank, uint32_t page, uint8_t* data_buffer, uint8_t* meta_buffer, uint8_t* correctedBits) {
    LOG_ACCESS("FIL_readSinglePage(0x%x, 0x%x, %p, %p)\n", bank, page, data_buffer, meta_buffer);

    cow_read(nand_bank[bank], data_buffer, page * PAGE_SIZE, PAGE_SIZE);
    cow_read(nand_spare[bank], meta_buffer, page * 16, 16);

    return is_page_all_FFs(meta_buffer);
}

uint32_t FIL_readNoECC(uint16_t bank, uint32_t page, uint8_t* data_buffer, uint8_t* meta_buffer) {
    LOG_ACCESS("FIL_readNoECC(0x%x, 0x%x, %p)\n", bank, page, data_buffer, meta_buffer);

    cow_read(nand_bank[bank], data_buffer, page * PAGE_SIZE, PAGE_SIZE);
    cow_read(nand_spare[bank], meta_buffer, page * 16, 16);

    return 0;
}

uint32_t FIL_readSequentialPages(uint16_t bank, uint32_t page, uint8_t* data_buffers, uint8_t* meta_buffers, uint8_t* correctedBits) {
    LOG_ACCESS("FIL_readSequentialPages(0x%x, 0x%x, %p, %p, %p)\n", bank, page, data_buffers, meta_buffers, correctedBits);
    return is_page_all_FFs(meta_buffers);
}

uint32_t FIL_readScatteredPages(uint16_t* banks, uint32_t* pages, uint8_t* data_buffers, uint8_t* meta_buffers, uint16_t count, uint8_t* correctedBits) {
    LOG_ACCESS("FIL_readScatteredPages(%p, %p, %p, %p, %d, %p)\n", banks, pages, data_buffers, meta_buffers, count, correctedBits);

    for (int i = 0; i < count; i++) {
        cow_read(nand_bank[banks[i]], data_buffers, pages[i] * PAGE_SIZE, PAGE_SIZE);
        cow_read(nand_spare[banks[i]], meta_buffers, pages[i] * 16, 16);
    }
    return is_page_all_FFs(meta_buffers);
}

uint32_t FIL_readSinglePageNoMetadata(uint16_t bank, uint32_t page, uint8_t* data_buffer) {
    LOG_ACCESS("FIL_readSinglePageNoMetadata(0x%x, 0x%x, %p)\n", bank, page, data_buffer);

    cow_read(nand_bank[bank], data_buffer, page * PAGE_SIZE, PAGE_SIZE);

    return is_page_all_FFs(data_buffer);
}

uint32_t FIL_writeScatteredPages(uint16_t* banks, uint32_t* pages, uint8_t* data_buffers, uint8_t* meta_buffers, uint16_t count) {
    LOG_ACCESS("FIL_writeScatteredPages(%p, %p, %p, %p, %d)\n", banks, pages, data_buffers, meta_buffers, count);

    for (int i = 0; i < count; i++) {
        cow_write(nand_bank[banks[i]], data_buffers, pages[i] * PAGE_SIZE, PAGE_SIZE);
        cow_write(nand_spare[banks[i]], meta_buffers, pages[i] * 16, 16);
    }

    return 0;
}


uint32_t FIL_writeSinglePage(uint16_t bank, uint32_t page, uint8_t* data_buffer, uint8_t* meta_buffer) {
    LOG_ACCESS("FIL_writeSinglePage(0x%x, 0x%x, %p, %p)\n", bank, page, data_buffer, meta_buffer);

    cow_write(nand_bank[bank], data_buffer, page * PAGE_SIZE, PAGE_SIZE);
    cow_write(nand_spare[bank], meta_buffer, page * 16, 16);

    return 0;
}

uint32_t FIL_writeSequentialPages(uint32_t* pages, uint8_t* data_buffers, uint8_t* meta_buffers, uint16_t count, uint8_t aligned) {
    LOG_ACCESS("FIL_writeSequentialPages(%p, %p, %p, %d, %d)\n", pages, data_buffers, meta_buffers, count, aligned);

    for (int i = 0; i < count; i += BANK_COUNT) {
        for (int bank = 0; bank < BANK_COUNT; bank++) {
            uint8_t* tmpDataBuf = data_buffers + ((i + bank) * PAGE_SIZE);
            uint8_t* tmpMetaBuf = meta_buffers + ((i + bank) * 12);

            LOG_ACCESS("\t");
            FIL_writeSinglePage(bank, pages[i + bank], tmpDataBuf, tmpMetaBuf);
        }
    }

    return 0;
}

uint32_t FIL_writeSinglePageNoMetadata(uint16_t bank, uint32_t page, uint8_t* data_buffer) {
    LOG_ACCESS("FIL_writeSinglePageNoMetadata(0x%x, 0x%x, %p)\n", bank, page, data_buffer);

    cow_write(nand_bank[bank], data_buffer, page * PAGE_SIZE, PAGE_SIZE);

    return 0;
}

uint32_t FIL_eraseSingleBlock(uint16_t bank, uint16_t block) {
    LOG_ACCESS("FIL_eraseSingleBlock(0x%x, 0x%x)\n", bank, block);

    return 0;
}

uint32_t FIL_eraseSequentialBlocks(uint16_t* banks, uint16_t* blocks, uint32_t count) {
    LOG_ACCESS("FIL_eraseSequentialBlocks(%p, %p, %d)\n", banks, blocks, count);
}

uint32_t FIL_resetAndVerifyIds() {
    LOG_ACCESS("FIL_resetAndVerifyIds()\n");
    return 0;
}

int main() {
    // get the size of nand-original.bin, allocate memory using mmap (setting the memory as executable), and read the file into memory
    FILE* fp = fopen("nand-original.bin", "rb");
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    progmem = mmap(0x0, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    fread(progmem, 1, size, fp);
    fclose(fp);

    // load the nand bank contents
    nand_bank[0] = cow_open("nand-dump-bank0.bin");
    nand_bank[1] = cow_open("nand-dump-bank1.bin");
    nand_bank[2] = cow_open("nand-dump-bank2.bin");
    nand_bank[3] = cow_open("nand-dump-bank3.bin");
    nand_spare[0] = cow_open("nand-dump-bank0-spare.bin");
    nand_spare[1] = cow_open("nand-dump-bank1-spare.bin");
    nand_spare[2] = cow_open("nand-dump-bank2-spare.bin");
    nand_spare[3] = cow_open("nand-dump-bank3-spare.bin");

    // allocate where the static global variables will be (this is probably bad because of MAP_FIXED, but it works on my machine™)
    staticmem = allocate_fixed(0x8000, 0xab60 - 0x8000);
    memcpy(staticmem, progmem + 0x8000, 0xab60 - 0x8000); // just in case we overwrote something statically allocated and initialized

    // allocate the NAND registers because some functions use them but we don't care
    void* nandregs = allocate_fixed(0x38a00000, 0x1000);

    // patch out FIL functions with things that read from a file instead
    ((uint16_t*) (progmem + 0x66a0))[0] = 0x4770; // bx lr from FIL_reset at 0x66a0    
    patch_function(0x676c, FIL_readIds);
    ((uint32_t*) (progmem + 0x75d4))[0] = FIL_readSinglePage;
    ((uint32_t*) (progmem + 0x75dc))[0] = FIL_readNoECC;
    ((uint32_t*) (progmem + 0x75e0))[0] = FIL_readSequentialPages;
    ((uint32_t*) (progmem + 0x75e4))[0] = FIL_readScatteredPages;
    ((uint32_t*) (progmem + 0x75fc))[0] = FIL_readSinglePageNoMetadata;
    ((uint32_t*) (progmem + 0x7604))[0] = FIL_resetAndVerifyIds;

    ((uint32_t*) (progmem + 0x75e8))[0] = FIL_writeScatteredPages;
    ((uint32_t*) (progmem + 0x75ec))[0] = FIL_writeSequentialPages;
    ((uint32_t*) (progmem + 0x75f0))[0] = FIL_writeSinglePage;
    ((uint32_t*) (progmem + 0x7600))[0] = FIL_writeSinglePageNoMetadata;

    ((uint32_t*) (progmem + 0x75f4))[0] = FIL_eraseSingleBlock;
    ((uint32_t*) (progmem + 0x75f8))[0] = FIL_eraseSequentialBlocks;

    // TODO: Fix this particular mess:
    // patch out the memory allocator with host malloc (staticmem @ 0x84c0 = *malloc)
    // I'm making a struct and pointing everything to my malloc and it works even though
    // I never use the struct. it probably puts it in a correct place on the stack coincidentally.
    struct mallocs {
        uint32_t malloc1;
        uint32_t malloc2;
    } mallocs = {&ipod_malloc, &ipod_malloc};
    uint32_t malloc_addr = (uint32_t) &ipod_malloc;
    ((uint32_t*) (staticmem + 0x84c0 - 0x8000))[0] = &malloc_addr;

    // patch out some driver signature pointers
    const char* nanddriversign = "NANDDRIVERSIGN";
    const char* deviceinfobbt = "DEVICEINFOBBT";
    ((uint32_t*) (progmem + 0x5c08))[0] = nanddriversign;
    ((uint32_t*) (progmem + 0x5650))[0] = deviceinfobbt;
    ((uint32_t*) (progmem + 0x5a9c))[0] = deviceinfobbt;

    // patch out some static function pointers
    // VFL CTX stuff
    for(int addr = 0x453c; addr <= 0x4548; addr += 4) {
        ((uint32_t*) (progmem + addr))[0] += progmem;
    }

    // page conversion functions
    for(int addr = 0x66d8; addr <= 0x6704; addr += 4) {
        ((uint32_t*) (progmem + addr))[0] += progmem;
    }

    // load up the relevant pointers to "exported" functions
    int (*AND_Init)(uint32_t*, uint16_t*) = (int (*)(uint32_t*, uint16_t*)) (progmem + 0x5c85);
    int (*FTL_Read)(uint32_t, uint32_t, uint8_t*) = (int (*)(uint32_t, uint32_t, uint8_t*)) (progmem + 0x126d);

    // call the function
    uint32_t nand_lba_out;
    uint16_t pages_per_block_exp_out;
    uint32_t ret = AND_Init(&nand_lba_out, &pages_per_block_exp_out);

    // print the results
    printf("ret: %d\n", ret);
    printf("nand_lba_out: %d\n", nand_lba_out);
    printf("pages_per_block_exp_out: %d\n", pages_per_block_exp_out);

    // read the first page
    uint8_t* buffer = malloc(PAGE_SIZE);
    FILE* ftldump = fopen("ftl-dump.bin", "wb");

    int i = 0;
    while(1) {
        ret = FTL_Read(i, 1, buffer);
        if(ret != 0) {
            printf("FTL_Read returned %d\n", ret);
            fclose(ftldump);
            return 0;
        }
        fwrite(buffer, 1, PAGE_SIZE, ftldump);
        i++;
    }
}