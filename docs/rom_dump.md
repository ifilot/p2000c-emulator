# Dumping the P2000C mainboard IPL ROM

[`tools/ipldump/IPLDUMP.ASM`](../tools/ipldump/IPLDUMP.ASM) is written for the
standard Digital
Research CP/M `ASM.COM` assembler. It uses only Intel 8080 mnemonics.

## Prepare the floppy

The emulator bundles `images/ipldump.flp`, a non-bootable writable floppy with
`ASM.COM`, `LOAD.COM`, and `IPLDUMP.ASM`. Mount it in drive B through **Media >
Drive B > Use IPL Dump Toolchain Floppy**. On a real P2000C, copy these three
files to a writable CP/M floppy with at least roughly 12 KiB free for the
assembler output, executable, and final dump.

The repository copy of `IPLDUMP.ASM` is already encoded as CP/M text: ASCII,
CR/LF line endings, no Unicode byte-order mark, and a final `1AH` end-of-file
byte. It can therefore be injected directly into a CP/M filesystem. A transfer
program's text/ASCII mode is also acceptable, provided it does not duplicate
the final marker. Do not use text conversion when retrieving `IPLDUMP.BIN`.

Every source line is at most 79 characters wide, so viewing or assembling the
file never depends on wrapping beyond the P2000C's 80-column display.

## Assemble and run

At the CP/M prompt:

```text
B>ASM IPLDUMP
B>LOAD IPLDUMP
B>IPLDUMP
```

`ASM.COM` should reach `END OF ASSEMBLY` without an error and create
`IPLDUMP.HEX`. `LOAD.COM` turns that file into `IPLDUMP.COM`. Running it creates
`IPLDUMP.BIN` on the current drive. The existing `IPLDUMP.BIN`, if any, is
replaced.

The COM file contains an intentional unused area. Its ROM-reading routine is
assembled at address `1000H`, beyond the mainboard ROM overlay. Do not relocate
or remove that `ORG 1000H` directive. The routine writes `00H` to memory-manager
port `1EH` to expose the ROM at `0000H`-`0FFFH`, then writes `02H` to restore
CP/M's all-RAM mapping before calling BDOS.

## Retrieve and verify

Copy `IPLDUMP.BIN` back to the host without text conversion. It must be exactly
4096 bytes. Preferably run the dumper twice, retrieve both results under
different host filenames, and compare them:

```sh
sha256sum ipldump-first.bin ipldump-second.bin
cmp ipldump-first.bin ipldump-second.bin
```

The last two bytes are the checksum stored by Philips firmware. The emulator
can perform the manual's checksum validation once the dump is available.

## Recovery

The program restores the RAM mapping before it makes any CP/M BDOS calls. If
the machine is interrupted or loses power during the short ROM-copy operation,
a normal reset restores the machine; the program does not write firmware or
other non-volatile memory. Disk errors are reported before returning to CP/M.
