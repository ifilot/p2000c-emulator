; IPLDUMP.ASM - Philips P2000C mainboard IPL ROM dumper
;
; Assemble on CP/M with:
;     ASM IPLDUMP
;     LOAD IPLDUMP
;     IPLDUMP
;
; The program creates IPLDUMP.BIN on the current drive.  The output is a
; headerless 4096-byte image of the mainboard IPL ROM at 0000H-0FFFH.
;
; This source uses Intel 8080 mnemonics accepted by Digital Research ASM.COM.

BDOS    EQU     0005H
FCBDEL  EQU     13H             ; Delete file
FCBCRE  EQU     16H             ; Make file
FCBCLS  EQU     10H             ; Close file
FCBWR   EQU     15H             ; Sequential write
SETDMA  EQU     1AH             ; Set DMA address
PRINT   EQU     09H             ; Print dollar-terminated string

MMPORT  EQU     1EH             ; P2000C memory-manager output port
ROMMAP  EQU     00H             ; IPL ROM visible at 0000H-0FFFH
RAMMAP  EQU     02H             ; Internal RAM visible everywhere

BUFFER  EQU     2000H           ; 4096-byte copy: 2000H-2FFFH
RECS    EQU     32              ; 32 CP/M records of 128 bytes

        ORG     0100H

START:  LXI     D,BANNER
        MVI     C,PRINT
        CALL    BDOS

; DUMPROM is assembled at 1000H.  It remains readable when the IPL ROM is
; exposed at 0000H-0FFFH.
        CALL    DUMPROM

        LXI     D,COPIED
        MVI     C,PRINT
        CALL    BDOS

; Delete an older dump, if present.  The delete result is deliberately ignored.
        LXI     D,FCB
        MVI     C,FCBDEL
        CALL    BDOS

; Restore all variable FCB fields in case the delete call changed them.
        LXI     H,FCB+12
        MVI     B,24
        XRA     A
CLRFCB: MOV     M,A
        INX     H
        DCR     B
        JNZ     CLRFCB

; Create an empty IPLDUMP.BIN on the current drive.
        LXI     D,FCB
        MVI     C,FCBCRE
        CALL    BDOS
        CPI     0FFH
        JZ      CREERR

; Write the buffer as 32 consecutive 128-byte CP/M records.
        LXI     H,BUFFER
        MVI     B,RECS
WRLOOP: PUSH    B
        PUSH    H
        MOV     D,H
        MOV     E,L
        MVI     C,SETDMA
        CALL    BDOS

        LXI     D,FCB
        MVI     C,FCBWR
        CALL    BDOS
        POP     H
        POP     B
        ORA     A
        JNZ     WRTERR

        LXI     D,0080H
        DAD     D
        DCR     B
        JNZ     WRLOOP

; A directory entry is not final until the file has been closed.
        LXI     D,FCB
        MVI     C,FCBCLS
        CALL    BDOS
        CPI     0FFH
        JZ      CLSERR

        LXI     D,DONE
        MVI     C,PRINT
        CALL    BDOS
        JMP     0000H

CREERR: LXI     D,CREMSG
        JMP     ERROR

WRTERR: LXI     D,FCB
        MVI     C,FCBCLS
        CALL    BDOS            ; Finalize any records already written
        LXI     D,WRTMSG
        JMP     ERROR

CLSERR: LXI     D,CLSMSG

ERROR:  MVI     C,PRINT
        CALL    BDOS
        JMP     0000H

BANNER: DB      13,10
        DB      'PHILIPS P2000C IPL ROM DUMPER',13,10
        DB      'READING 0000H-0FFFH...$'
COPIED: DB      13,10,'ROM COPIED; WRITING IPLDUMP.BIN...$'
DONE:   DB      13,10,'IPLDUMP.BIN CREATED (4096 BYTES).',13,10,'$'
CREMSG: DB      13,10,'ERROR: CANNOT CREATE IPLDUMP.BIN.',13,10,'$'
WRTMSG: DB      13,10,'ERROR: DISK FULL OR WRITE FAILED.',13,10,'$'
CLSMSG: DB      13,10,'ERROR: CANNOT CLOSE IPLDUMP.BIN.',13,10,'$'

; Standard 36-byte CP/M file control block.  Drive zero means current drive.
FCB:    DB      0,'IPLDUMP ','BIN'
        DS      24

; Keep this routine outside the 4 KiB ROM overlay.  Interrupts remain disabled
; until RAM is visible again so no interrupt can observe the temporary mapping.
        ORG     1000H

DUMPROM:
        DI
        XRA     A
        OUT     MMPORT          ; Expose IPL ROM for reads

        LXI     H,0000H
        LXI     D,BUFFER
        LXI     B,1000H
CPYROM: MOV     A,M
        STAX    D
        INX     H
        INX     D
        DCX     B
        MOV     A,B
        ORA     C
        JNZ     CPYROM

        MVI     A,RAMMAP
        OUT     MMPORT          ; Restore normal CP/M RAM mapping
        EI
        RET

        END     START
