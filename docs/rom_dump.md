# Dumping the P2000C mainboard IPL ROM

[`tools/IPLDUMP.ASM`](../tools/IPLDUMP.ASM) is written for the standard Digital
Research CP/M `ASM.COM` assembler. It uses only Intel 8080 mnemonics.

## Prepare the floppy

Copy the source to a writable P2000C CP/M floppy under the name
`IPLDUMP.ASM`. Ensure that the disk has at least roughly 12 KiB free for the
source, assembler output, executable, and final dump.

The repository copy of `IPLDUMP.ASM` is already encoded as CP/M text: ASCII,
CR/LF line endings, no Unicode byte-order mark, and a final `1AH` end-of-file
byte. It can therefore be injected directly into a CP/M filesystem. A transfer
program's text/ASCII mode is also acceptable, provided it does not duplicate
the final marker. Do not use text conversion when retrieving `IPLDUMP.BIN`.

Every source line is limited to 79 columns. This keeps the complete line on the
P2000C's 80-column terminal without invoking right-margin wrapping.
Every source line is at most 79 characters wide, so viewing or assembling the
file never depends on wrapping beyond the P2000C's 80-column display.

## Assemble and run

At the CP/M prompt:

```text
A>ASM IPLDUMP
A>LOAD IPLDUMP
A>IPLDUMP
```

`ASM.COM` should report zero errors and create `IPLDUMP.HEX`. `LOAD.COM` turns
that file into `IPLDUMP.COM`. Running it creates `IPLDUMP.BIN` on the current
drive. The existing `IPLDUMP.BIN`, if any, is replaced.

The COM file contains an intentional unused area. Its ROM-reading routine is
assembled at address `1000H`, beyond the mainboard ROM overlay. Do not relocate
or remove that `ORG 1000H` directive.

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
