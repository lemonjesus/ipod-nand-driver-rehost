# iPod Nano 3rd Generation NAND Driver Rehost

This exceptionally unportable code is an attempt at rehosting the EFI NAND Driver from the iPod Nano 3rd Generation. It patches and blindly calls into a precompiled binary blob, which is not included in this repository. It appears that, for the most part, the driver completely works. It's unclear if the 'sequential' family of FIL functions works as intended, since they seem to behave differently from the other FIL functions.

This is meant to run on a 32-bit ARM platform. I use a Raspberry Pi 3, but any 32-bit ARM platform that supports THUMB should work.

## Building

```
gcc main.c -D_LARGE_FILE_SOURCE=1 -mcpu=cortex-a7 -mthumb -Wno-int-conversion -g -o main
```

## Getting the EFI NAND Driver

The driver is stored in the iPod's firmware, which can be extracted and decrypted using wInd3x from the EFI stored in NOR. You can then use UEFITool or something like it to extract the driver from the EFI firmware. Extract the binary under `945F3B55-95A5-4BD9-AEED-6EC243B813A8`. The SHA1 of the driver in question is `67c9ab9acf688534ef7744ff025f91c1882ede34`.

## Getting the NAND Contents

This program also requires the NAND contents of the iPod. The current best way to dump your NAND is over serial with a custom Rockbox bootloader that dumps each bank and each spare area one at a time. Each bank is stored in its own file alongside its spare file.

## Purpose and Uses

This is useful for data recovery, as I've been able to mount filesystems from the image and read some files. See the code for how to do this. Basically, you call `FTL_Read()` with increasing page numbers until it returns non-zero and write them to a large image file:

```c
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
```

## Customization

You can enable/disable logs with the `ENABLE_ACCESS_LOGGING` constant at the top of the file. If you want to witness how an iPod formats a totally blank chip, I modified my File-COW to pretend to be completely blank (all FFs when a area not previously written to is read). You can switch between `file-cow.h` and `ff-cow.h` to get different behaviors. The former will read from a real NAND dump, and the latter will pretend to be a blank NAND chip and always force the driver to format the chip from scratch.

# Things I Had To Patch
1. A lot of variables in the NAND driver are statically allocated and there are too many references across it to reliably patch them all. So I made mmap give me specific addresses with `MAP_FIXED`. It's definitely a code smell, but the kernel doesn't seem to mind. I also did this for the NAND DMA region (`0x38A00000`) because some functions write directly to these registers in ways we don't care about.
1. I patched the pointers to all of the FIL functions so they read/write to the NAND file instead of a real device.
1. I patched the pointer to the memory allocation service so it'd just use regular `malloc`. The way it's called from the EFI has a garbage parameter in front of it, so I made a wrapper function to ignore it.
1. Patched some other function pointers so they didn't point to inaccessible memory regions. There are some other pointers to strings (like `DEVICEINFOBBT`) that I patched to point to a constant string in the host program.

## Portability to Other iPods

This technique is portable to other iPods, but this code makes use of very specific offsets into the NAND Driver blob that are specific to the N3G. For your specific iPod, you'd need to know what those offsets are and where the iPod expects some memory addresses to exist and make those modifications.

## Future Work
 - Support other iPods.
 - Fully support both 'sequential' FIL calls. They don't fully work at the moment, but neither are required to FTL dump a chip which is what this project is mostly used for.
