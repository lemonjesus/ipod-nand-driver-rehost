# iPod Nano 3rd Generation NAND Driver Rehost

This exceptionally unportable code is an attempt at rehosting the EFI NAND Driver from the iPod Nano 3rd Generation. It patches and blindly calls into a precompiled binary blob, which is not included in this repository. In its current state, this code is not meant to be replicatable. I haven't even verified that it totally works myself.

## Getting the EFI NAND Driver

The driver is stored in the iPod's firmware, which can be extracted and decrypted using wInd3x from the EFI stored in NOR. You can then use UEFITool to extract the driver from the EFI firmware. Extract the binary under `945F3B55-95A5-4BD9-AEED-6EC243B813A8`. The SHA1 of the driver in question is `67c9ab9acf688534ef7744ff025f91c1882ede34`.

## Getting the NAND Contents

This program also requires the NAND contents of the iPod. The current best way to dump your NAND is over serial with a custom Rockbox bootloader that dumps each bank and each spare area one at a time. Each bank is stored in its own file alongside its spare file.
