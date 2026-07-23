; P2EDIT.ASM - small CP/M text editor for the Philips P2000C
;
; Assemble on the P2000C with Digital Research's tools:
;     ASM P2EDIT
;     LOAD P2EDIT
;     P2EDIT [D:]FILE.TXT
;
; The interface follows the compact style of X16 Edit and GNU nano.  It uses
; documented CP/M 2.2 BDOS calls and documented P2000C terminal controls only.
;
; Text is stored as LF-delimited bytes.  CR is discarded while loading and
; restored before LF while saving.  The CP/M 1AH end marker is not kept in RAM.
;
; Buffer invariants:
;   closed gap: BUFBEG through BUFEND-1 is one contiguous text stream
;   open gap:   text is below GAPLO and from GAPHI through BUFLIM-1
;               GAPLO through GAPHI-1 is free. CURPTR=GAPLO, BUFEND=BUFLIM
; TOPPTR is the first logical line displayed in the 20-row text viewport.

BDOS    EQU     0005H
CONOUT  EQU     02H
DIRIO   EQU     06H
OPENF   EQU     0FH
CLOSEF  EQU     10H
DELETE  EQU     13H
READSEQ EQU     14H
WRITSEQ EQU     15H
MAKEF   EQU     16H
RENAME  EQU     17H
GETDRV  EQU     19H
SETDMA  EQU     1AH

LF      EQU     0AH
CR      EQU     0DH
EOFCHR  EQU     1AH

TEXTROW EQU     20
HDRCOL  EQU     26
NORMATT EQU     40H
INVATT  EQU     50H

; P2EDIT occupies the low TPA and currently ends below 1A00H.  The initial
; stack at 2FF0H leaves over 5 KiB for stack use and future code growth.
; Text uses 3000H-BFFFH (36 KiB).  C000H is reserved by Philips CP/M.
BUFBEG  EQU     3000H
BUFLIM  EQU     0C000H
STACK   EQU     2FF0H

        ORG     0100H

; Initialise an empty, closed gap buffer before parsing the CP/M command tail.
START:  LXI     SP,STACK
        LXI     H,BUFBEG
        SHLD    BUFEND
        SHLD    CURPTR
        SHLD    TOPPTR
        XRA     A
        STA     NAMED
        STA     MODIF
        STA     TRUNC
        STA     MSGON
        STA     GAPOPEN
        STA     LEFTCOL
        STA     LEFTCOL+1
        LXI     H,1
        SHLD    LINENUM
        SHLD    TOTLINES
        CALL    GETTAIL
        MVI     C,GETDRV
        CALL    BDOS
        INR     A
        STA     CURDRV
        LDA     NAMED
        ORA     A
        JZ      STNEW
        CALL    LOADFIL
        JMP     STDRAW
STNEW:  LDA     MSGON
        ORA     A
        JNZ     STDRAW
        LXI     H,NEWMSG
        CALL    SETMSG
STDRAW: CALL    DRAW

; The dispatcher uses the native P2000C control bytes.  It snapshots the
; viewport and message state so refresh paths can choose the least work.
MAIN:   CALL    GETKEY
        STA     KEYTMP
        LHLD    TOPPTR
        SHLD    OLDTOP
        LHLD    LEFTCOL
        SHLD    OLDLEFT
        LDA     MSGON
        STA     OLDMSG
        XRA     A
        STA     MSGON
        LDA     KEYTMP
        CPI     01H
        JZ      KHOME
        CPI     02H
        JZ      KWRDL
        CPI     04H
        JZ      KEND
        CPI     05H
        JZ      KWRDR
        CPI     06H
        JZ      KRIGHT
        CPI     07H
        JZ      KHELP
        CPI     08H
        JZ      KBACK
        CPI     09H
        JZ      KTAB
        CPI     0AH
        JZ      KDOWN
        CPI     0BH
        JZ      KCUT
        CPI     0DH
        JZ      KENTER
        CPI     0EH
        JZ      KNEW
        CPI     0FH
        JZ      KSAVE
        CPI     10H
        JZ      KPASTE
        CPI     12H
        JZ      KOPEN
        CPI     15H
        JZ      KLEFT
        CPI     16H
        JZ      KPGDN
        CPI     17H
        JZ      KFIND
        CPI     18H
        JZ      KEXIT
        CPI     19H
        JZ      KPGUP
        CPI     1AH
        JZ      KUP
        CPI     1BH
        JZ      KHELP
        CPI     7FH
        JZ      KDEL
        CPI     20H
        JC      MAIN
        CPI     7FH
        JNC     MAIN
        CALL    INSERT
        ORA     A
        JZ      EDITREF
        CALL    COLINC
        JMP     EDITREF

KHOME:  CALL    CLOSEGAP
        LHLD    CURPTR
        CALL    LINSTRT
        SHLD    CURPTR
        CALL    COLZERO
        JMP     NAVREF

KEND:   CALL    CLOSEGAP
        LHLD    CURPTR
        CALL    LINEND
        SHLD    CURPTR
        CALL    GETCOL
        JMP     NAVREF

; Ctrl-Left skips separators to the left, then backs over one word.  Crossing
; the separator before that word is undone so the cursor rests at its start.
KWRDL:
KWLSP:  CALL    CURLEFT
        LDA     MOVOK
        ORA     A
        JZ      KWLDON
        LDA     MOVCHR
        CALL    ISSPAC
        JNZ     KWLWRD
        LDA     MOVCHR
        CPI     LF
        CZ      DECLIN
        JMP     KWLSP
KWLWRD: CALL    CURLEFT
        LDA     MOVOK
        ORA     A
        JZ      KWLDON
        LDA     MOVCHR
        CALL    ISSPAC
        JNZ     KWLWRD
        CALL    CURRIGHT
KWLDON: CALL    GETCOL
        JMP     NAVREF

; Ctrl-Right first leaves the current word, then skips separators until the
; next word.  Line state is maintained when a skipped separator is LF.
KWRDR:
KWRRWD: CALL    PEEKCUR
        LDA     MOVOK
        ORA     A
        JZ      KWRDON
        LDA     MOVCHR
        CALL    ISSPAC
        JZ      KWRRSP
        CALL    CURRIGHT
        JMP     KWRRWD
KWRRSP: CALL    PEEKCUR
        LDA     MOVOK
        ORA     A
        JZ      KWRDON
        LDA     MOVCHR
        CALL    ISSPAC
        JNZ     KWRDON
        CALL    CURRIGHT
        LDA     MOVCHR
        CPI     LF
        CZ      INCLIN
        JMP     KWRRSP
KWRDON: CALL    GETCOL
        JMP     NAVREF

; Return Z when A is a word separator: space, tab, or line feed.
ISSPAC: CPI     ' '
        RZ
        CPI     09H
        RZ
        CPI     LF
        RET

KLEFT:  CALL    CURLEFT
        LDA     MOVOK
        ORA     A
        JZ      NAVREF
        LDA     MOVCHR
        CPI     LF
        JZ      KLEFTLF
        CPI     09H
        JZ      KLEFTTB
        CALL    COLDEC
        JMP     NAVREF
KLEFTLF:
        CALL    DECLIN
KLEFTTB:
        CALL    GETCOL
        JMP     NAVREF

KRIGHT: CALL    CURRIGHT
        LDA     MOVOK
        ORA     A
        JZ      NAVREF
        LDA     MOVCHR
        CPI     LF
        JZ      KRGHTLF
        CPI     09H
        JZ      KRGHTTB
        CALL    COLINC
        JMP     NAVREF
KRGHTLF:
        CALL    INCLIN
        CALL    COLZERO
        JMP     NAVREF
KRGHTTB:
        CALL    COLTAB
        JMP     NAVREF

KUP:    CALL    CLOSEGAP
        CALL    UPONE
        CALL    GETCOL
        JMP     NAVREF

KDOWN:  CALL    CLOSEGAP
        CALL    DOWNONE
        CALL    GETCOL
        JMP     NAVREF

KPGUP:  CALL    CLOSEGAP
        MVI     B,TEXTROW-1
KPU1:   PUSH    B
        CALL    UPONE
        POP     B
        DCR     B
        JNZ     KPU1
        CALL    GETCOL
        JMP     NAVREF

KPGDN:  CALL    CLOSEGAP
        MVI     B,TEXTROW-1
KPD1:   PUSH    B
        CALL    DOWNONE
        POP     B
        DCR     B
        JNZ     KPD1
        CALL    GETCOL
        JMP     NAVREF

KBACK:  XRA     A
        STA     EDITLF
        STA     EDITTAB
        STA     MOVOK
        LHLD    CURPTR
        LXI     D,BUFBEG
        CALL    CMPHD
        JZ      KBEDIT
        MVI     A,1
        STA     MOVOK
        DCX     H
        MOV     A,M
        CPI     LF
        JZ      KBACKLF
        CPI     09H
        JNZ     KBEDIT
        MVI     A,1
        STA     EDITTAB
        JMP     KBEDIT
KBACKLF:
        MVI     A,1
        STA     EDITLF
KBEDIT: CALL    BACKSP
        LDA     MOVOK
        ORA     A
        JZ      EDITREF
        LDA     EDITLF
        ORA     A
        JZ      KBACKTB
        CALL    DECLIN
        CALL    GETCOL
        JMP     KREDRAW
KBACKTB:
        LDA     EDITTAB
        ORA     A
        JZ      KBACKCH
        CALL    GETCOL
        JMP     EDITREF
KBACKCH:
        CALL    COLDEC
        JMP     EDITREF

KDEL:   XRA     A
        STA     EDITLF
        CALL    PEEKCUR
        LDA     MOVOK
        ORA     A
        JZ      KDEDIT
        LDA     MOVCHR
        CPI     LF
        JNZ     KDEDIT
        MVI     A,1
        STA     EDITLF
KDEDIT: CALL    DELCUR
        LDA     EDITLF
        ORA     A
        JZ      EDITREF
        JMP     KREDRAW

KENTER: MVI     A,LF
        CALL    INSERT
        ORA     A
        JZ      KREDRAW
        CALL    COLZERO
        JMP     KREDRAW

KTAB:   CALL    GETCOL
        LDA     COLNUM
        ANI     07H
        MOV     B,A
        MVI     A,8
        SUB     B
        MOV     B,A
KTAB1:  PUSH    B
        MVI     A,' '
        CALL    INSERT
        POP     B
        DCR     B
        JNZ     KTAB1
        CALL    GETCOL
        JMP     EDITREF

KCUT:   CALL    CLOSEGAP
        CALL    CUTLINE
        CALL    GETCOL
        JMP     KREDRAW

KPASTE: CALL    PASTELN
        CALL    GETCOL
        JMP     KREDRAW

KSAVE:  CALL    SAVEFIL
        JMP     STATREF

KOPEN:  CALL    OPENNEW
        CALL    GETCOL
        JMP     KHEAD

KNEW:   CALL    NEWBUF
        CALL    GETCOL
        JMP     KHEAD

KFIND:  CALL    CLOSEGAP
        CALL    FINDTXT
        CALL    GETPOS
        JMP     NAVREF

KHELP:  CALL    HELPSCR
        JMP     KFULL

KEXIT:  CALL    DOEXIT
        JMP     KREDRAW

; Refresh tiers: DRAW repaints everything, DRAWVIEW omits the static footer,
; DRAWHEAD additionally rebuilds the filename area, and DRAWONE changes one
; text row.  Most navigation and typing stays on the two cheapest paths.
KREDRAW:
        CALL    ENSURE
        CALL    DRAWVIEW
        JMP     MAIN

KFULL:  CALL    ENSURE
        CALL    DRAW
        JMP     MAIN

KHEAD:  CALL    ENSURE
        CALL    DRAWHEAD
        JMP     MAIN

; Navigation leaves text unchanged. Repaint it only when the viewport moved.
NAVREF: CALL    ENSURE
        CALL    VIEWCHG
        ORA     A
        JNZ     KREDRAW
        CALL    HIDECUR
        CALL    DRAWDYN
        CALL    UPDSTAT
        CALL    PLACECUR
        JMP     MAIN

; Ordinary insert/delete operations affect one logical line. Avoid the full
; buffer-position scan and repaint only that line unless scrolling is needed.
EDITREF:
        CALL    ENSURE
        CALL    VIEWCHG
        ORA     A
        JNZ     KREDRAW
        CALL    HIDECUR
        CALL    DRAWDYN
        CALL    DRAWONE
        CALL    UPDSTAT
        CALL    PLACECUR
        JMP     MAIN

; Commands such as Write change only header/status presentation.
STATREF:
        CALL    HIDECUR
        CALL    DRAWHDR
        CALL    DRAWSTAT
        CALL    PLACECUR
        JMP     MAIN

; Repaint the status row when a command created a message or when this key
; cleared the message that was displayed previously.
UPDSTAT:
        LDA     OLDMSG
        MOV     B,A
        LDA     MSGON
        ORA     B
        RZ
        JMP     DRAWSTAT

; ---------------------------------------------------------------------------
; Cursor movement and buffer editing

; Vertical movement updates the maintained current-line counter only when the
; cursor actually changed lines.  LINENUM/TOTLINES avoid rescanning the file
; for each header update.  COLNUM is likewise maintained during local motion.
UPONE:  LHLD    CURPTR
        SHLD    NAVOLD
        CALL    MOVEUP
        LHLD    CURPTR
        XCHG
        LHLD    NAVOLD
        CALL    CMPHD
        RZ
        JMP     DECLIN

DOWNONE:
        LHLD    CURPTR
        SHLD    NAVOLD
        CALL    MOVEDN
        LHLD    CURPTR
        XCHG
        LHLD    NAVOLD
        CALL    CMPHD
        RZ
        JMP     INCLIN

INCLIN: LHLD    LINENUM
        INX     H
        SHLD    LINENUM
        RET

DECLIN: LHLD    LINENUM
        DCX     H
        SHLD    LINENUM
        RET

INCTOT: LHLD    TOTLINES
        INX     H
        SHLD    TOTLINES
        RET

DECTOT: LHLD    TOTLINES
        DCX     H
        SHLD    TOTLINES
        RET

COLZERO:
        LXI     H,0
        SHLD    COLNUM
        RET

COLINC: LHLD    COLNUM
        INX     H
        SHLD    COLNUM
        RET

COLDEC: LHLD    COLNUM
        DCX     H
        SHLD    COLNUM
        RET

COLTAB: LHLD    COLNUM
        LXI     D,8
        DAD     D
        MOV     A,L
        ANI     0F8H
        MOV     L,A
        SHLD    COLNUM
        RET

; Move vertically to the same byte offset where possible, or the new line end.
; GETCOL later converts that raw offset to a visual column when tabs occur.
MOVEUP: LHLD    CURPTR
        CALL    LINSTRT
        SHLD    LINETMP
        LXI     D,BUFBEG
        CALL    CMPHD
        RZ
        XCHG
        LHLD    CURPTR
        MOV     A,L
        SUB     E
        STA     WANTCOL
        MOV     A,H
        SBB     D
        STA     WANTCOL+1
        XCHG
        DCX     H
        CALL    LINSTRT
        SHLD    MOVEST
        CALL    LINEND
        SHLD    MOVEEND
        JMP     MOVETGT

MOVEDN: LHLD    CURPTR
        CALL    LINSTRT
        XCHG
        LHLD    CURPTR
        MOV     A,L
        SUB     E
        STA     WANTCOL
        MOV     A,H
        SBB     D
        STA     WANTCOL+1
        XCHG
        CALL    LINEND
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        RZ
        INX     H
        SHLD    MOVEST
        CALL    LINEND
        SHLD    MOVEEND

MOVETGT:
        LHLD    MOVEEND
        XCHG
        LHLD    MOVEST
        MOV     A,E
        SUB     L
        MOV     C,A
        MOV     A,D
        SBB     H
        MOV     B,A
        LDA     WANTCOL+1
        CMP     B
        JC      MTWANT
        JNZ     MTEND
        LDA     WANTCOL
        CMP     C
        JC      MTWANT
        JZ      MTEND
MTEND:  XCHG
        SHLD    CURPTR
        RET
MTWANT: LHLD    MOVEST
        LDA     WANTCOL
        MOV     E,A
        LDA     WANTCOL+1
        MOV     D,A
        DAD     D
        SHLD    CURPTR
        RET

; Insert A at the cursor.  The first local edit opens all remaining free RAM as
; a gap and moves the suffix once.  Later local edits touch only the gap edges.
INSERT: STA     INSTMP
        CALL    OPENGAP
        LDA     GAPOPEN
        ORA     A
        JZ      INSFULL
        LHLD    GAPLO
        XCHG
        LHLD    GAPHI
        CALL    CMPHD
        JZ      INSFULL
        XCHG
        LDA     INSTMP
        MOV     M,A
        INX     H
        SHLD    GAPLO
        SHLD    CURPTR
        MVI     A,1
        STA     MODIF
        LDA     INSTMP
        CPI     LF
        JNZ     INSGOOD
        CALL    INCLIN
        CALL    INCTOT
INSGOOD:
        MVI     A,1
        RET
INSFULL:
        LXI     H,FULLMSG
        CALL    SETMSG
        XRA     A
        RET

BACKSP: LHLD    CURPTR
        LXI     D,BUFBEG
        CALL    CMPHD
        RZ
        LDA     GAPOPEN
        ORA     A
        JZ      BACKOLD
        LHLD    GAPLO
        DCX     H
        MOV     A,M
        STA     DELTMP
        SHLD    GAPLO
        SHLD    CURPTR
        JMP     DELMARK
BACKOLD:
        DCX     H
        SHLD    CURPTR
        JMP     DELCUR

; Delete byte at CURPTR.
DELCUR: LDA     GAPOPEN
        ORA     A
        JZ      DELOLD
        LHLD    GAPHI
        LXI     D,BUFLIM
        CALL    CMPHD
        RZ
        MOV     A,M
        STA     DELTMP
        INX     H
        SHLD    GAPHI
        JMP     DELMARK
DELOLD: LHLD    CURPTR
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        RZ
        LHLD    CURPTR
        MOV     A,M
        STA     DELTMP
        LHLD    BUFEND
        MOV     B,H
        MOV     C,L
        LHLD    CURPTR
        XCHG
        LHLD    CURPTR
        INX     H
DELSHFT:
        MOV     A,H
        CMP     B
        JNZ     DELCOPY
        MOV     A,L
        CMP     C
        JZ      DELDONE
DELCOPY:
        MOV     A,M
        STAX    D
        INX     H
        INX     D
        JMP     DELSHFT
DELDONE:
        LHLD    BUFEND
        DCX     H
        SHLD    BUFEND
DELMARK:
        MVI     A,1
        STA     MODIF
        LDA     DELTMP
        CPI     LF
        RNZ
        JMP     DECTOT

; Open the gap by copying the suffix backwards to the top of the text area:
;
;   before: [prefix|suffix|free...............................]
;   after:  [prefix|gap........................|suffix at top]
;
; Copying backwards permits overlap.  The gap then contains every unused byte.
OPENGAP:
        LDA     GAPOPEN
        ORA     A
        RNZ
        LHLD    BUFEND
        LXI     D,BUFLIM
        CALL    CMPHD
        RZ
        LHLD    CURPTR
        SHLD    GAPLO
        MOV     B,H
        MOV     C,L
        LHLD    BUFEND
        XCHG
        LXI     H,BUFLIM
        MOV     A,L
        SUB     E
        MOV     L,A
        MOV     A,H
        SBB     D
        MOV     H,A
        XCHG
        LHLD    CURPTR
        DAD     D
        SHLD    GAPHI
        LHLD    CURPTR
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        JZ      GAPSET
        LHLD    BUFEND
        DCX     H
        LXI     D,BUFLIM-1
GAPBACK:
        MOV     A,M
        STAX    D
        MOV     A,H
        CMP     B
        JNZ     GAPBNX
        MOV     A,L
        CMP     C
        JZ      GAPSET
GAPBNX: DCX     H
        DCX     D
        JMP     GAPBACK
GAPSET: LXI     H,BUFLIM
        SHLD    BUFEND
        MVI     A,1
        STA     GAPOPEN
        RET

; Compact the two occupied segments when a command needs contiguous text.
; Saving, searching, line cut, and vertical line scans deliberately pay this
; one-off cost.  Sustained local editing keeps the gap open.
CLOSEGAP:
        LDA     GAPOPEN
        ORA     A
        RZ
        LHLD    GAPLO
        XCHG
        LHLD    GAPHI
GAPFWD: LXI     B,BUFLIM
        MOV     A,H
        CMP     B
        JNZ     GAPFCP
        MOV     A,L
        CMP     C
        JZ      GAPCLO
GAPFCP: MOV     A,M
        STAX    D
        INX     H
        INX     D
        JMP     GAPFWD
GAPCLO: XCHG
        SHLD    BUFEND
        LHLD    CURPTR
        SHLD    GAPLO
        SHLD    GAPHI
        XRA     A
        STA     GAPOPEN
        RET

; Move the cursor and gap one byte left/right.  MOVOK reports success and
; MOVCHR is the crossed byte, allowing the caller to maintain line state.
CURLEFT:
        XRA     A
        STA     MOVOK
        LHLD    CURPTR
        LXI     D,BUFBEG
        CALL    CMPHD
        RZ
        LDA     GAPOPEN
        ORA     A
        JZ      CURLOLD
        LHLD    GAPLO
        DCX     H
        MOV     A,M
        STA     MOVCHR
        SHLD    GAPLO
        SHLD    CURPTR
        LHLD    GAPHI
        DCX     H
        SHLD    GAPHI
        LDA     MOVCHR
        MOV     M,A
        JMP     CUROK
CURLOLD:
        LHLD    CURPTR
        DCX     H
        MOV     A,M
        STA     MOVCHR
        SHLD    CURPTR
CUROK:  MVI     A,1
        STA     MOVOK
        RET

CURRIGHT:
        CALL    PEEKCUR
        LDA     MOVOK
        ORA     A
        RZ
        LDA     GAPOPEN
        ORA     A
        JZ      CURROLD
        LHLD    GAPHI
        MOV     A,M
        PUSH    PSW
        INX     H
        SHLD    GAPHI
        LHLD    GAPLO
        POP     PSW
        MOV     M,A
        INX     H
        SHLD    GAPLO
        SHLD    CURPTR
        RET
CURROLD:
        LHLD    CURPTR
        INX     H
        SHLD    CURPTR
        RET

PEEKCUR:
        XRA     A
        STA     MOVOK
        LDA     GAPOPEN
        ORA     A
        JZ      PEEKOLD
        LHLD    GAPHI
        LXI     D,BUFLIM
        CALL    CMPHD
        RZ
        JMP     PEEKGET
PEEKOLD:
        LHLD    CURPTR
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        RZ
PEEKGET:
        MOV     A,M
        STA     MOVCHR
        MVI     A,1
        STA     MOVOK
        RET

; Copy and remove the current logical line.  Its LF is included unless this is
; the final unterminated line.  Clipboard lines are limited to 255 bytes.
CUTLINE:
        XRA     A
        STA     CUTLF
        LHLD    CURPTR
        CALL    LINSTRT
        SHLD    MOVEST
        CALL    LINEND
        SHLD    MOVEEND
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        JZ      CUTLEN
        INX     H
        SHLD    MOVEEND
CUTLEN: XCHG
        LHLD    MOVEST
        MOV     A,E
        SUB     L
        MOV     C,A
        MOV     A,D
        SBB     H
        ORA     A
        JNZ     CUTLONG
        MOV     A,C
        ORA     A
        JZ      CUTZERO
        STA     CLIPLEN
        MOV     B,A
        LXI     D,CLIPBUF
CUTCOPY:
        MOV     A,M
        STAX    D
        CPI     LF
        JNZ     CUTCPN
        MVI     A,1
        STA     CUTLF
CUTCPN:
        INX     H
        INX     D
        DCR     B
        JNZ     CUTCOPY
        LHLD    BUFEND
        MOV     B,H
        MOV     C,L
        LHLD    MOVEST
        XCHG
        LHLD    MOVEEND
CUTMOVE:
        MOV     A,H
        CMP     B
        JNZ     CUTCOPY1
        MOV     A,L
        CMP     C
        JZ      CUTTAIL
CUTCOPY1:
        MOV     A,M
        STAX    D
        INX     H
        INX     D
        JMP     CUTMOVE
CUTTAIL:
        LHLD    BUFEND
        LDA     CLIPLEN
        MOV     E,A
        MVI     D,0
        MOV     A,L
        SUB     E
        MOV     L,A
        MOV     A,H
        SBB     D
        MOV     H,A
        SHLD    BUFEND
        LHLD    MOVEST
        SHLD    CURPTR
        LHLD    BUFEND
        XCHG
        LHLD    CURPTR
        CALL    CMPHD
        JC      CUTMOD
        XCHG
        SHLD    CURPTR
CUTMOD: MVI     A,1
        STA     MODIF
        LDA     CUTLF
        ORA     A
        JZ      CUTMSG1
        CALL    DECTOT
CUTMSG1:
        LXI     H,CUTMSG
        CALL    SETMSG
        RET
CUTZERO:
        XRA     A
        STA     CLIPLEN
        RET
CUTLONG:
        LXI     H,LONGMSG
        CALL    SETMSG
        RET

; Paste through INSERT so gap capacity, line totals, and modified state follow
; exactly the same path as typed text.
PASTELN:
        LDA     CLIPLEN
        ORA     A
        JZ      PASTNON
        MOV     B,A
        LXI     H,CLIPBUF
PASTLP: MOV     A,M
        INX     H
        PUSH    H
        PUSH    B
        CALL    INSERT
        POP     B
        POP     H
        DCR     B
        JNZ     PASTLP
        LXI     H,PASTMSG
        CALL    SETMSG
        RET
PASTNON:
        LXI     H,NOCLIP
        CALL    SETMSG
        RET

; Return start/end of the logical line containing pointer HL.  LINEND and all
; forward iterators cross an open gap through SKIPGAP.
LINSTRT:
        PUSH    D
LSLOOP: LXI     D,BUFBEG
        CALL    CMPHD
        JZ      LSDONE
        DCX     H
        MOV     A,M
        CPI     LF
        JZ      LSNEXT
        JMP     LSLOOP
LSNEXT: INX     H
LSDONE: POP     D
        RET

LINEND: PUSH    D
LELOOP: CALL    SKIPGAP
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        JZ      LEDONE
        MOV     A,M
        CPI     LF
        JZ      LEDONE
        INX     H
        JMP     LELOOP
LEDONE: POP     D
        RET

; Translate the first byte of the gap to the first byte of the suffix.
; Other physical addresses are returned unchanged.
SKIPGAP:
        LDA     GAPOPEN
        ORA     A
        RZ
        PUSH    D
        XCHG
        LHLD    GAPLO
        CALL    CMPHD
        JNZ     GAPNOSK
        LHLD    GAPHI
        POP     D
        RET
GAPNOSK:
        XCHG
        POP     D
        RET

; Compare HL with DE, preserving both.  Flags match unsigned HL-DE.
CMPHD:  MOV     A,H
        CMP     D
        RNZ
        MOV     A,L
        CMP     E
        RET

; ---------------------------------------------------------------------------
; Viewport calculations and screen drawing

; Keep CURPTR visible.  TOPPTR moves by logical lines, while LEFTCOL moves in
; 40-column chunks.  CURROW is cached for cursor placement and DRAWONE.
ENSURE:
        LHLD    TOPPTR
        XCHG
        LHLD    CURPTR
        CALL    CMPHD
        JNC     ENSCNT
        CALL    LINSTRT
        SHLD    TOPPTR
ENSCNT: LHLD    TOPPTR
        MVI     B,0
ENSLP:  XCHG
        LHLD    CURPTR
        CALL    CMPHD
        XCHG
        JZ      ENSROWS
        MOV     A,M
        CPI     LF
        JNZ     ENSNX
        INR     B
ENSNX:  INX     H
        JMP     ENSLP
ENSROWS:
        MOV     A,B
        CPI     TEXTROW
        JC      ENSROW
        LHLD    TOPPTR
        CALL    NEXTLIN
        SHLD    TOPPTR
        DCR     B
        JMP     ENSROWS
ENSROW: MOV     A,B
        STA     CURROW
        LHLD    LEFTCOL
        XCHG
        LHLD    COLNUM
        CALL    CMPHD
        JNC     ENSRGT
        CALL    LEFTCHN
        RET
ENSRGT: LXI     D,79
        LHLD    LEFTCOL
        DAD     D
        XCHG
        LHLD    COLNUM
        CALL    CMPHD
        JC      ENSDONE
        JZ      ENSDONE
        LXI     D,40
        MOV     A,L
        SUB     E
        MOV     L,A
        MOV     A,H
        SBB     D
        MOV     H,A
        SHLD    LEFTCOL
ENSDONE:
        RET

; Keep the cursor near the middle after crossing either horizontal edge.
; A 40-column step avoids a full viewport redraw for every character.
LEFTCHN:
        MOV     A,H
        ORA     A
        JNZ     LEFTSUB
        MOV     A,L
        CPI     40
        JC      LEFTZER
LEFTSUB:
        LXI     D,40
        MOV     A,L
        SUB     E
        MOV     L,A
        MOV     A,H
        SBB     D
        MOV     H,A
        SHLD    LEFTCOL
        RET
LEFTZER:
        LXI     H,0
        SHLD    LEFTCOL
        RET

NEXTLIN:
        CALL    LINEND
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        RZ
        INX     H
        RET

; Compute one-based line and zero-based visual column at CURPTR.
GETPOS: LXI     H,1
        SHLD    LINENUM
        LXI     H,BUFBEG
        SHLD    POSPTR
GPLOOP: LHLD    POSPTR
        XCHG
        LHLD    CURPTR
        CALL    CMPHD
        XCHG
        JZ      GETCOL
        MOV     A,M
        CPI     LF
        JNZ     GPNEXT
        PUSH    H
        LHLD    LINENUM
        INX     H
        SHLD    LINENUM
        POP     H
GPNEXT: INX     H
        SHLD    POSPTR
        JMP     GPLOOP

GETCOL: LHLD    CURPTR
        CALL    LINSTRT
        SHLD    GCPTR
        XRA     A
        STA     COLNUM
        STA     COLNUM+1
GCLOOP: LHLD    GCPTR
        XCHG
        LHLD    CURPTR
        CALL    CMPHD
        XCHG
        RZ
        MOV     A,M
        CPI     09H
        JZ      GCTAB
        INX     H
        SHLD    GCPTR
        LHLD    COLNUM
        INX     H
        SHLD    COLNUM
        JMP     GCLOOP
GCTAB:  INX     H
        SHLD    GCPTR
        LHLD    COLNUM
        LXI     D,8
        DAD     D
        MOV     A,L
        ANI     0F8H
        MOV     L,A
        SHLD    COLNUM
        JMP     GCLOOP

HIDECUR:
        MVI     A,1BH
        CALL    PUTC
        MVI     A,'c'
        JMP     PUTC

; Physical screen layout:
;   row 0 header, rows 1-20 text, row 21 status, rows 22-23 shortcuts.
; DRAWVIEW never emits the shortcut rows, so scrolling cannot disturb them.
DRAW:   MVI     A,1
        STA     FOOTON
        STA     HEADON
        JMP     DRAWENT

; Refresh header, text viewport and status without rewriting the static menu.
DRAWVIEW:
        XRA     A
        STA     FOOTON
        STA     HEADON
        JMP     DRAWENT

; Refresh a changed filename/header without repainting the static footer.
DRAWHEAD:
        XRA     A
        STA     FOOTON
        INR     A
        STA     HEADON
DRAWENT:
        CALL    HIDECUR
        LDA     HEADON
        ORA     A
        JZ      DRAWDY1
        CALL    DRAWHDR
        JMP     DRAWHDN
DRAWDY1:
        CALL    DRAWDYN
DRAWHDN:
        LHLD    TOPPTR
        SHLD    DRAWPTR
        MVI     A,1
        STA     DRAWROW
DRLOOP: LDA     DRAWROW
        CPI     TEXTROW+1
        JZ      DRFOOT
        MOV     B,A
        MVI     C,0
        CALL    POS
        MVI     A,NORMATT
        CALL    SETATTR
        CALL    ERASE
        CALL    DRAWLIN
        LDA     DRAWROW
        INR     A
        STA     DRAWROW
        JMP     DRLOOP
DRFOOT: CALL    DRAWSTAT
        LDA     FOOTON
        ORA     A
        CNZ     DRAWHELP
        JMP     PLACECUR

; Redraw only the logical line containing the cursor.
DRAWONE:
        LDA     CURROW
        INR     A
        MOV     B,A
        MVI     C,0
        CALL    POS
        MVI     A,NORMATT
        CALL    SETATTR
        CALL    ERASE
        LHLD    CURPTR
        CALL    LINSTRT
        SHLD    DRAWPTR
        JMP     DRAWLIN

PLACECUR:
        LDA     CURROW
        INR     A
        MOV     B,A
        LHLD    COLNUM
        XCHG
        LHLD    LEFTCOL
        MOV     A,E
        SUB     L
        MOV     C,A
        CALL    POS
        MVI     A,NORMATT
        CALL    SETATTR
        MVI     A,1BH
        CALL    PUTC
        MVI     A,'C'
        CALL    PUTC
        RET

; Return non-zero when a scrolling operation changed the viewport.
VIEWCHG:
        LHLD    TOPPTR
        XCHG
        LHLD    OLDTOP
        CALL    CMPHD
        JNZ     VCYES
        LHLD    LEFTCOL
        XCHG
        LHLD    OLDLEFT
        CALL    CMPHD
        JNZ     VCYES
        XRA     A
        RET
VCYES:  MVI     A,1
        RET

DRAWHDR:
        MVI     B,0
        MVI     C,0
        CALL    POS
        MVI     A,INVATT
        CALL    SETATTR
        CALL    ERASE
        LXI     H,APPTIT
        CALL    PRINTZ
        LDA     NAMED
        ORA     A
        JZ      DHNEW
        LXI     H,FILEFCB
        CALL    PRNAME
        JMP     DRAWDYN
DHNEW:  LXI     H,NONAME
        CALL    PRINTZ
        JMP     DRAWDYN

; The filename is static during ordinary editing.  Rewrite only the changing
; modified marker, line counters and column field at a fixed header position.
DRAWDYN:
        MVI     B,0
        MVI     C,HDRCOL
        CALL    POS
        MVI     A,INVATT
        CALL    SETATTR
        CALL    ERASE
DHMOD:  LDA     MODIF
        ORA     A
        JZ      DHCLEAN
        LXI     H,MODTXT
        CALL    PRINTZ
        JMP     DHPOS
DHCLEAN:LXI     H,CLNTXT
        CALL    PRINTZ
DHPOS:  LXI     H,LNTXT
        CALL    PRINTZ
        LHLD    LINENUM
        XRA     A
        STA     LEAD
        CALL    PRINT16
        MVI     A,'/'
        CALL    PUTC
        LHLD    TOTLINES
        XRA     A
        STA     LEAD
        CALL    PRINT16
        LXI     H,COLTXT
        CALL    PRINTZ
        LHLD    COLNUM
        INX     H
        XRA     A
        STA     LEAD
        CALL    PRINT16
        MVI     A,NORMATT
        CALL    SETATTR
        RET

DRAWSTAT:
        MVI     B,21
        MVI     C,0
        CALL    POS
        MVI     A,NORMATT
        CALL    SETATTR
        CALL    ERASE
        LDA     MSGON
        ORA     A
        JZ      DSDEF
        LXI     H,MSGBUF
        JMP     PRINTZ
DSDEF:  LXI     H,DEFSTAT
        JMP     PRINTZ

DRAWHELP:
        MVI     B,22
        MVI     C,0
        CALL    POS
        MVI     A,INVATT
        CALL    SETATTR
        CALL    ERASE
        LXI     H,HELP1
        CALL    PRINTZ
        MVI     B,23
        MVI     C,0
        CALL    POS
        CALL    ERASE
        LXI     H,HELP2
        CALL    PRINTZ
        MVI     A,NORMATT
        CALL    SETATTR
        RET

; Draw one logical line from DRAWPTR.  VISCOL tracks the complete visual line,
; including tab expansion.  OUTCOL counts only characters emitted on screen.
; Once 80 columns are visible, scan directly to LF without drawing the tail.
DRAWLIN:
        XRA     A
        STA     VISCOL
        STA     VISCOL+1
        STA     OUTCOL
        LHLD    DRAWPTR
DLLP:   CALL    SKIPGAP
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        JZ      DLDONE
        MOV     A,M
        CPI     LF
        JZ      DLNL
        CPI     09H
        JZ      DLTAB
        MOV     E,A
        CALL    DLEMIT
        INX     H
        LDA     OUTCOL
        CPI     80
        JC      DLLP
        JMP     DLSKIP
DLTAB:  PUSH    H
        LHLD    VISCOL
        LXI     D,8
        DAD     D
        MOV     A,L
        ANI     0F8H
        STA     TABEND
        MOV     A,H
        STA     TABEND+1
        POP     H
DLTAB1: MVI     E,' '
        CALL    DLEMIT
        LHLD    VISCOL
        XCHG
        LHLD    TABEND
        CALL    CMPHD
        XCHG
        JNZ     DLTAB1
        INX     H
        LDA     OUTCOL
        CPI     80
        JC      DLLP
DLSKIP: CALL    SKIPGAP
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        JZ      DLDONE
        MOV     A,M
        INX     H
        CPI     LF
        JNZ     DLSKIP
        SHLD    DRAWPTR
        RET
DLNL:   INX     H
        SHLD    DRAWPTR
        RET
DLDONE: SHLD    DRAWPTR
        RET

; Advance visual column and emit E once it lies inside the horizontal window.
DLEMIT: PUSH    H
        MOV     A,E
        STA     DRAWC
        LHLD    LEFTCOL
        XCHG
        LHLD    VISCOL
        CALL    CMPHD
        JC      DLENO
        LDA     OUTCOL
        CPI     80
        JNC     DLENO
        LDA     DRAWC
        CPI     20H
        JNC     DLEPR
        MVI     A,'.'
DLEPR:  CALL    PUTC
        LDA     OUTCOL
        INR     A
        STA     OUTCOL
DLENO:  LHLD    VISCOL
        INX     H
        SHLD    VISCOL
        POP     H
        RET

; ---------------------------------------------------------------------------
; File name parsing and CP/M file operations

GETTAIL:
        LDA     005DH
        CPI     ' '
        RZ
        ORA     A
        RZ
        LXI     H,005CH
        LXI     D,FILEFCB
        CALL    COPY36
        MVI     A,1
        STA     NAMED
        RET

; Parse INBUF into NEWFCB.  Returns A=1 on success.
PARSNEW:
        LXI     H,NEWFCB
        MVI     B,36
        XRA     A
PNCLR:  MOV     M,A
        INX     H
        DCR     B
        JNZ     PNCLR
        LDA     CURDRV
        STA     NEWFCB
        LXI     H,NEWFCB+1
        MVI     B,11
        MVI     A,' '
PNFILL: MOV     M,A
        INX     H
        DCR     B
        JNZ     PNFILL
        XRA     A
        STA     BASECNT
        STA     EXTCNT
        STA     PART
        LXI     H,INBUF
        MOV     A,M
        ORA     A
        JZ      PNBAD
        CALL    UPPER
        STA     PARTCHR
        INX     H
        MOV     A,M
        CPI     ':'
        JNZ     PNNODR
        LDA     PARTCHR
        CPI     'A'
        JC      PNBAD
        CPI     'P'+1
        JNC     PNBAD
        SUI     'A'-1
        STA     NEWFCB
        INX     H
        JMP     PNLOOP
PNNODR: DCX     H
PNLOOP: MOV     A,M
        ORA     A
        JZ      PNEND
        CPI     '.'
        JZ      PNDOT
        CPI     21H
        JC      PNBAD
        CPI     7FH
        JNC     PNBAD
        CPI     '*'
        JZ      PNBAD
        CPI     '?'
        JZ      PNBAD
        CPI     '/'
        JZ      PNBAD
        CPI     ':'
        JZ      PNBAD
        CPI     ';'
        JZ      PNBAD
        CPI     '<'
        JZ      PNBAD
        CPI     '='
        JZ      PNBAD
        CPI     '>'
        JZ      PNBAD
        CPI     '['
        JZ      PNBAD
        CPI     ']'
        JZ      PNBAD
        CALL    UPPER
        MOV     C,A
        LDA     PART
        ORA     A
        JNZ     PNNEXT
        LDA     BASECNT
        CPI     8
        JNC     PNBAD
        MOV     E,A
        MVI     D,0
        PUSH    H
        LXI     H,NEWFCB+1
        DAD     D
        MOV     M,C
        POP     H
        LDA     BASECNT
        INR     A
        STA     BASECNT
        INX     H
        JMP     PNLOOP
PNNEXT: LDA     EXTCNT
        CPI     3
        JNC     PNBAD
        MOV     E,A
        MVI     D,0
        PUSH    H
        LXI     H,NEWFCB+9
        DAD     D
        MOV     M,C
        POP     H
        LDA     EXTCNT
        INR     A
        STA     EXTCNT
        INX     H
        JMP     PNLOOP
PNDOT:  LDA     PART
        ORA     A
        JNZ     PNBAD
        LDA     BASECNT
        ORA     A
        JZ      PNBAD
        MVI     A,1
        STA     PART
        INX     H
        JMP     PNLOOP
PNEND:  LDA     BASECNT
        ORA     A
        JZ      PNBAD
        LDA     PART
        ORA     A
        JZ      PNGOOD
        LDA     EXTCNT
        ORA     A
        JZ      PNBAD
PNGOOD: MVI     A,1
        RET
PNBAD:  LXI     H,BADNAME
        CALL    SETMSG
        XRA     A
        RET

UPPER:  CPI     'a'
        RC
        CPI     'z'+1
        RNC
        SUI     20H
        RET

; Read sequential 128-byte CP/M records into contiguous text.  CR and the 1AH
; end marker are file-format bytes rather than editor bytes.  If BUFLIM is
; reached, retain a read-only prefix so Save cannot destroy the original.
LOADFIL:
        CALL    CLOSEGAP
        CALL    RESETFC
        LXI     D,FILEFCB
        MVI     C,OPENF
        CALL    BDOS
        CPI     0FFH
        JZ      LOADNEW
        LXI     H,BUFBEG
        SHLD    BUFEND
        SHLD    CURPTR
        SHLD    TOPPTR
        LXI     H,1
        SHLD    LINENUM
        SHLD    TOTLINES
        XRA     A
        STA     GAPOPEN
        STA     TRUNC
LOADREC:
        LXI     D,DISKBUF
        MVI     C,SETDMA
        CALL    BDOS
        LXI     D,FILEFCB
        MVI     C,READSEQ
        CALL    BDOS
        ORA     A
        JNZ     LOADDON
        LXI     H,DISKBUF
        MVI     B,128
LOADBYT:
        MOV     A,M
        CPI     EOFCHR
        JZ      LOADDON
        CPI     CR
        JZ      LOADNXT
        MOV     C,A
        LHLD    BUFEND
        LXI     D,BUFLIM
        CALL    CMPHD
        JZ      LOADFUL
        MOV     A,C
        MOV     M,A
        INX     H
        SHLD    BUFEND
        MOV     A,C
        CPI     LF
        CZ      INCTOT
        LXI     H,DISKBUF
        LDA     LOADIDX
        MOV     E,A
        MVI     D,0
        DAD     D
LOADNXT:
        INX     H
        LDA     LOADIDX
        INR     A
        STA     LOADIDX
        DCR     B
        JNZ     LOADBYT
        XRA     A
        STA     LOADIDX
        JMP     LOADREC
LOADFUL:
        MVI     A,1
        STA     TRUNC
LOADDON:
        LXI     D,FILEFCB
        MVI     C,CLOSEF
        CALL    BDOS
        XRA     A
        STA     GAPOPEN
        STA     MODIF
        STA     LOADIDX
        LDA     TRUNC
        ORA     A
        JZ      LOADOK
        LXI     H,TRUNMSG
        CALL    SETMSG
        RET
LOADOK: LXI     H,READMSG
        CALL    SETMSG
        RET
LOADNEW:
        LXI     H,BUFBEG
        SHLD    BUFEND
        SHLD    CURPTR
        SHLD    TOPPTR
        LXI     H,1
        SHLD    LINENUM
        SHLD    TOTLINES
        XRA     A
        STA     MODIF
        STA     TRUNC
        LXI     H,NEWFMSG
        CALL    SETMSG
        RET

RESETFC:
        LXI     H,FILEFCB+12
        MVI     B,24
        XRA     A
RFLOOP: MOV     M,A
        INX     H
        DCR     B
        JNZ     RFLOOP
        RET

; Save through a temporary FCB on the destination drive.  Only after all
; records close successfully is the old name deleted and the temporary file
; renamed, reducing the chance of losing the original after an I/O failure.
SAVEFIL:
        CALL    CLOSEGAP
        LDA     TRUNC
        ORA     A
        JZ      SVNAME
        LXI     H,NOSAVEM
        CALL    SETMSG
        RET
SVNAME: LDA     NAMED
        ORA     A
        JNZ     SAVEDO
        LXI     H,WRITEPM
        CALL    INPUT
        ORA     A
        RZ
        CALL    PARSNEW
        ORA     A
        RZ
        LXI     H,NEWFCB
        LXI     D,FILEFCB
        CALL    COPY36
        MVI     A,1
        STA     NAMED
SAVEDO: CALL    BUILDTMP
        LXI     D,TMPFCB
        MVI     C,DELETE
        CALL    BDOS
        LXI     D,TMPFCB
        MVI     C,MAKEF
        CALL    BDOS
        CPI     0FFH
        JZ      SAVEERR
        LXI     H,BUFBEG
        SHLD    SAVEPTR
        LXI     H,DISKBUF
        SHLD    OUTPTR
        XRA     A
        STA     OUTCNT
        STA     IOERR
SVLOOP: LHLD    SAVEPTR
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        JZ      SVEOF
        MOV     A,M
        INX     H
        SHLD    SAVEPTR
        CPI     LF
        JNZ     SVCHAR
        MVI     A,CR
        CALL    OUTBYTE
        MVI     A,LF
SVCHAR: CALL    OUTBYTE
        LDA     IOERR
        ORA     A
        JNZ     SVCLOSE
        JMP     SVLOOP
SVEOF:  MVI     A,EOFCHR
        CALL    OUTBYTE
SVPAD:  LDA     OUTCNT
        ORA     A
        JZ      SVCLOSE
        MVI     A,EOFCHR
        CALL    OUTBYTE
        JMP     SVPAD
SVCLOSE:
        LXI     D,TMPFCB
        MVI     C,CLOSEF
        CALL    BDOS
        CPI     0FFH
        JNZ     SVCHK
        MVI     A,1
        STA     IOERR
SVCHK:  LDA     IOERR
        ORA     A
        JNZ     SAVEERR
        CALL    RESETFC
        LXI     D,FILEFCB
        MVI     C,DELETE
        CALL    BDOS
        CALL    BUILDRN
        LXI     D,TMPFCB
        MVI     C,RENAME
        CALL    BDOS
        CPI     0FFH
        JZ      SAVEREN
        XRA     A
        STA     MODIF
        LXI     H,WROTE1
        CALL    SETMSG
        LXI     H,MSGBUF
        CALL    STRLEN
        XCHG
        LXI     H,FILEFCB
        CALL    NAMEBUF
        RET
SAVEERR:
        LXI     D,TMPFCB
        MVI     C,DELETE
        CALL    BDOS
        LXI     H,SAVEMSG
        CALL    SETMSG
        RET
SAVEREN:
        LXI     H,RENAMEM
        CALL    SETMSG
        RET

; Append A to the 128-byte output record and flush it through BDOS when full.
OUTBYTE:
        PUSH    B
        PUSH    D
        PUSH    H
        MOV     B,A
        LHLD    OUTPTR
        MOV     M,B
        INX     H
        SHLD    OUTPTR
        LDA     OUTCNT
        INR     A
        STA     OUTCNT
        CPI     128
        JNZ     OUTDONE
        LXI     D,DISKBUF
        MVI     C,SETDMA
        CALL    BDOS
        LXI     D,TMPFCB
        MVI     C,WRITSEQ
        CALL    BDOS
        ORA     A
        JZ      OUTRST
        MVI     A,1
        STA     IOERR
OUTRST: LXI     H,DISKBUF
        SHLD    OUTPTR
        XRA     A
        STA     OUTCNT
OUTDONE:
        POP     H
        POP     D
        POP     B
        RET

BUILDTMP:
        LXI     H,TMPFCB
        MVI     B,36
        XRA     A
BTCLR:  MOV     M,A
        INX     H
        DCR     B
        JNZ     BTCLR
        LDA     FILEFCB
        STA     TMPFCB
        LXI     H,TMPNAME
        LXI     D,TMPFCB+1
        MVI     B,11
BTCPY:  MOV     A,M
        STAX    D
        INX     H
        INX     D
        DCR     B
        JNZ     BTCPY
        RET

BUILDRN:
        LXI     H,TMPFCB+12
        MVI     B,24
        XRA     A
BRCLR:  MOV     M,A
        INX     H
        DCR     B
        JNZ     BRCLR
        LDA     FILEFCB
        STA     TMPFCB+16
        LXI     H,FILEFCB+1
        LXI     D,TMPFCB+17
        MVI     B,11
BRCPY:  MOV     A,M
        STAX    D
        INX     H
        INX     D
        DCR     B
        JNZ     BRCPY
        RET

OPENNEW:
        CALL    DISOK
        ORA     A
        RZ
        LXI     H,OPENPM
        CALL    INPUT
        ORA     A
        RZ
        CALL    PARSNEW
        ORA     A
        RZ
        LXI     H,NEWFCB
        LXI     D,FILEFCB
        CALL    COPY36
        MVI     A,1
        STA     NAMED
        XRA     A
        STA     LEFTCOL
        STA     LEFTCOL+1
        CALL    LOADFIL
        RET

NEWBUF: CALL    DISOK
        ORA     A
        RZ
        LXI     H,BUFBEG
        SHLD    BUFEND
        SHLD    CURPTR
        SHLD    TOPPTR
        LXI     H,1
        SHLD    LINENUM
        SHLD    TOTLINES
        XRA     A
        STA     GAPOPEN
        STA     NAMED
        STA     MODIF
        STA     TRUNC
        STA     LEFTCOL
        STA     LEFTCOL+1
        LXI     H,NEWMSG
        CALL    SETMSG
        RET

; Confirm discarding a modified buffer.  Returns A=1 when operation may go on.
DISOK:  LDA     MODIF
        ORA     A
        JZ      DISYES
        LXI     H,DISCARD
        CALL    YESNO
        RET
DISYES: MVI     A,1
        RET

; ---------------------------------------------------------------------------
; Search, help and exit

; Forward, case-sensitive search over contiguous text.  The dispatcher closes
; the gap before entering, so the inner comparison can use simple addresses.
FINDTXT:
        LXI     H,FINDPM
        CALL    INPUT
        ORA     A
        RZ
        LHLD    CURPTR
        SHLD    FINDPTR
FINDNXT:
        LHLD    FINDPTR
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        JZ      FINDNO
        SHLD    CANDPTR
        LXI     D,INBUF
FINDCMP:
        LDAX    D
        ORA     A
        JZ      FINDFND
        PUSH    D
        XCHG
        LHLD    BUFEND
        CALL    CMPHD
        XCHG
        POP     D
        JZ      FINDNO
        LDAX    D
        CMP     M
        JNZ     FINDINC
        INX     H
        INX     D
        JMP     FINDCMP
FINDINC:
        LHLD    FINDPTR
        INX     H
        SHLD    FINDPTR
        JMP     FINDNXT
FINDFND:
        LHLD    CANDPTR
        SHLD    CURPTR
        LXI     H,FOUNDMS
        CALL    SETMSG
        RET
FINDNO: LXI     H,NOTFOUND
        CALL    SETMSG
        RET

HELPSCR:
        MVI     A,0CH
        CALL    PUTC
        MVI     A,INVATT
        CALL    SETATTR
        LXI     H,HELPTIT
        CALL    PRINTZ
        MVI     A,NORMATT
        CALL    SETATTR
        LXI     H,HELPTXT
        CALL    PRINTZ
        CALL    GETKEY
        RET

DOEXIT: LDA     MODIF
        ORA     A
        JZ      EXITNOW
        LXI     H,SAVEASK
        CALL    ASK3
        CPI     'C'
        RZ
        CPI     'N'
        JZ      EXITNOW
        CALL    SAVEFIL
        LDA     MODIF
        ORA     A
        RNZ
EXITNOW:
        MVI     A,NORMATT
        CALL    SETATTR
        MVI     A,0CH
        CALL    PUTC
        MVI     A,1BH
        CALL    PUTC
        MVI     A,'C'
        CALL    PUTC
        LXI     H,BYEMSG
        CALL    PRINTZ
        JMP     0000H

; ---------------------------------------------------------------------------
; Prompts, text and terminal helpers

INPUT:  PUSH    H
        CALL    PROMROW
        POP     H
        CALL    PRINTZ
        LXI     H,INBUF
        MVI     M,0
        SHLD    INPTR
        XRA     A
        STA     INLEN
INKEY:  CALL    GETKEY
        CPI     1BH
        JZ      INCAN
        CPI     CR
        JZ      INDONE
        CPI     08H
        JZ      INBACK
        CPI     7FH
        JZ      INBACK
        CPI     20H
        JC      INKEY
        CPI     7FH
        JNC     INKEY
        MOV     B,A
        LDA     INLEN
        CPI     31
        JNC     INKEY
        MOV     A,B
        LHLD    INPTR
        MOV     M,A
        INX     H
        MVI     M,0
        SHLD    INPTR
        LDA     INLEN
        INR     A
        STA     INLEN
        MOV     A,B
        CALL    PUTC
        JMP     INKEY
INBACK: LDA     INLEN
        ORA     A
        JZ      INKEY
        DCR     A
        STA     INLEN
        LHLD    INPTR
        DCX     H
        MVI     M,0
        SHLD    INPTR
        MVI     A,08H
        CALL    PUTC
        MVI     A,' '
        CALL    PUTC
        MVI     A,08H
        CALL    PUTC
        JMP     INKEY
INDONE: LDA     INLEN
        ORA     A
        RZ
        MVI     A,1
        RET
INCAN:  XRA     A
        RET

YESNO:  CALL    PROMROW
        CALL    PRINTZ
YNKEY:  CALL    GETKEY
        CALL    UPPER
        CPI     'Y'
        JZ      YNYES
        CPI     'N'
        JNZ     YNKEY
        XRA     A
        RET
YNYES:  MVI     A,1
        RET

; Ask for Y/N/C and return the uppercase character.
ASK3:   CALL    PROMROW
        CALL    PRINTZ
A3KEY:  CALL    GETKEY
        CALL    UPPER
        CPI     'Y'
        RZ
        CPI     'N'
        RZ
        CPI     'C'
        JNZ     A3KEY
        RET

PROMROW:
        MVI     A,1BH
        CALL    PUTC
        MVI     A,'c'
        CALL    PUTC
        MVI     B,21
        MVI     C,0
        CALL    POS
        MVI     A,INVATT
        CALL    SETATTR
        CALL    ERASE
        RET

SETMSG: LXI     D,MSGBUF
        MVI     B,79
SMCOPY: MOV     A,M
        ORA     A
        JZ      SMDONE
        STAX    D
        INX     H
        INX     D
        DCR     B
        JNZ     SMCOPY
SMDONE: XRA     A
        STAX    D
        MVI     A,1
        STA     MSGON
        RET

COPY36: MVI     B,36
C36LP:  MOV     A,M
        STAX    D
        INX     H
        INX     D
        DCR     B
        JNZ     C36LP
        RET

PRNAME: MOV     A,M
        ORA     A
        JZ      PNDDEF
        ADI     'A'-1
        CALL    PUTC
        MVI     A,':'
        CALL    PUTC
PNDDEF: INX     H
        MVI     B,8
PNBASE: MOV     A,M
        ANI     7FH
        CPI     ' '
        CZ      PNVOID
        CNZ     PUTC
        INX     H
        DCR     B
        JNZ     PNBASE
        MOV     A,M
        ANI     7FH
        CPI     ' '
        RZ
        MVI     A,'.'
        CALL    PUTC
        MVI     B,3
PNEXT:  MOV     A,M
        ANI     7FH
        CPI     ' '
        CZ      PNVOID
        CNZ     PUTC
        INX     H
        DCR     B
        JNZ     PNEXT
PNVOID: RET

; Append an FCB name to a zero-terminated destination DE.
NAMEBUF:
        PUSH    D
        MOV     A,M
        ORA     A
        JZ      NBDEF
        ADI     'A'-1
        STAX    D
        INX     D
        MVI     A,':'
        STAX    D
        INX     D
NBDEF:  INX     H
        MVI     B,8
NBBAS:  MOV     A,M
        CPI     ' '
        JZ      NBBNX
        STAX    D
        INX     D
NBBNX:  INX     H
        DCR     B
        JNZ     NBBAS
        MOV     A,M
        CPI     ' '
        JZ      NBDONE
        MVI     A,'.'
        STAX    D
        INX     D
        MVI     B,3
NBEXT:  MOV     A,M
        CPI     ' '
        JZ      NBEXNX
        STAX    D
        INX     D
NBEXNX: INX     H
        DCR     B
        JNZ     NBEXT
NBDONE: XRA     A
        STAX    D
        POP     D
        RET

STRLEN: MOV     A,M
        ORA     A
        RZ
        INX     H
        JMP     STRLEN

PRINT16:
        LXI     D,10000
        CALL    PDIGIT
        LXI     D,1000
        CALL    PDIGIT
        LXI     D,100
        CALL    PDIGIT
        LXI     D,10
        CALL    PDIGIT
        MOV     A,L
        ADI     '0'
        JMP     PUTC

PDIGIT: MVI     B,0
PDLOOP: MOV     A,H
        CMP     D
        JC      PDDONE
        JNZ     PDSUB
        MOV     A,L
        CMP     E
        JC      PDDONE
PDSUB:  MOV     A,L
        SUB     E
        MOV     L,A
        MOV     A,H
        SBB     D
        MOV     H,A
        INR     B
        JMP     PDLOOP
PDDONE: LDA     LEAD
        ORA     A
        JNZ     PDOUT
        MOV     A,B
        ORA     A
        RZ
        MVI     A,1
        STA     LEAD
PDOUT:  MOV     A,B
        ADI     '0'
        JMP     PUTC

PRINTZ: MOV     A,M
        ORA     A
        RZ
        CALL    PUTC
        INX     H
        JMP     PRINTZ

ERASE:  MVI     A,1BH
        CALL    PUTC
        MVI     A,'K'
        JMP     PUTC

SETATTR:MOV     B,A
        MVI     A,1BH
        CALL    PUTC
        MVI     A,'0'
        CALL    PUTC
        MOV     A,B
        JMP     PUTC

POS:    PUSH    B
        MVI     A,1BH
        CALL    PUTC
        MVI     A,'Y'
        CALL    PUTC
        POP     B
        MOV     A,B
        ADI     20H
        CALL    PUTC
        MOV     A,C
        ADI     20H
        JMP     PUTC

PUTC:   PUSH    B
        PUSH    D
        PUSH    H
        MOV     E,A
        MVI     C,CONOUT
        CALL    BDOS
        POP     H
        POP     D
        POP     B
        RET

GETKEY: PUSH    B
        PUSH    D
        PUSH    H
GKWAIT: MVI     E,0FFH
        MVI     C,DIRIO
        CALL    BDOS
        ORA     A
        JZ      GKWAIT
        POP     H
        POP     D
        POP     B
        RET

; ---------------------------------------------------------------------------
; Display strings and program state

APPTIT: DB      ' P2EDIT  ',0
NONAME: DB      '<new buffer>',0
MODTXT: DB      '  *',0
CLNTXT: DB      '   ',0
LNTXT:  DB      '   Ln ',0
COLTXT: DB      ' Col ',0
DEFSTAT:
        DB      'P2000C cursor keys move; TAB inserts spaces; DEL deletes',0
HELP1:  DB      '^G Help  ^O Write  ^R Open  ^Y PgUp  ^K Cut line'
        DB      '  ^W Find',0
HELP2:  DB      '^X Exit  ^N New    ^V PgDn  ^P Paste   Home/End'
        DB      '  BS/DEL',0

HELPTIT:
        DB      ' P2EDIT help',13,10,13,10,0
HELPTXT:
        DB      'Cursor keys    Move through the text',13,10
        DB      'Ctrl-Left/Right  Move to previous/next word',13,10
        DB      'Home / End    Start or end of the current line',13,10
        DB      'PgUp / PgDn  Move one screen',13,10
        DB      'Backspace     Delete left of the cursor',13,10
        DB      'Delete        Delete under the cursor',13,10
        DB      'Return        Insert a line break',13,10
        DB      'Tab           Insert spaces to the next tab stop',13,10,13,10
        DB      '^O  Write file          ^R  Open file',13,10
        DB      '^N  New buffer          ^W  Find text',13,10
        DB      '^K  Cut current line    ^P  Paste line',13,10
        DB      '^Y  Previous page       ^V  Next page',13,10
        DB      '^X  Exit                ^G/Esc Help',13,10,13,10
        DB      'The * in the header means the buffer has changed.',13,10
        DB      'Text capacity: 36 KiB. Files are written with CR/LF.',13,10
        DB      'Search is forward and case-sensitive.',13,10,13,10
        DB      'Press any key to return to the editor.',0

NEWMSG: DB      'New buffer',0
NEWFMSG:DB      'New file',0
READMSG:DB      'File read',0
TRUNMSG:DB      'File is too large; loaded prefix is read-only',0
NOSAVEM:DB      'Cannot write a truncated file; open or create a new buffer',0
FULLMSG:DB      'Text buffer full',0
BADNAME:DB      'Invalid CP/M filename',0
WRITEPM:DB      'Write file: ',0
OPENPM: DB      'Open file: ',0
FINDPM: DB      'Find: ',0
DISCARD:DB      'Discard unsaved changes? (Y/N) ',0
SAVEASK:DB      'Save changes before exit? (Y/N/C) ',0
SAVEMSG:DB      'Disk error while writing; original file was not replaced',0
RENAMEM:DB      'Saved as $P2EDIT$.$$$ but could not install final name',0
WROTE1: DB      'Wrote ',0
CUTMSG: DB      'Line cut',0
PASTMSG:DB      'Clipboard pasted',0
NOCLIP: DB      'Clipboard is empty',0
LONGMSG:DB      'Line is too long for the 255-byte clipboard',0
FOUNDMS:DB      'Found',0
NOTFOUND:
        DB      'Text not found after cursor',0
BYEMSG: DB      'P2EDIT finished.',13,10,0
TMPNAME:DB      '$P2EDIT$$$$'

; One-byte editor, redraw, and movement state.
NAMED:  DB      0
MODIF:  DB      0
TRUNC:  DB      0
MSGON:  DB      0
OLDMSG: DB      0
GAPOPEN:DB     0
CURDRV: DB      1
KEYTMP: DB      0
CURROW: DB      0
EDITLF: DB      0
EDITTAB:DB     0
DELTMP: DB      0
CUTLF:  DB      0
FOOTON: DB      0
HEADON: DB      0
MOVOK:  DB      0
MOVCHR: DB      0
DRAWROW:DB      0
OUTCOL: DB      0
DRAWC:  DB      0
INSTMP: DB      0

; Maintained document position and horizontal viewport state.
WANTCOL:DW     0
LINENUM:DW     1
TOTLINES:DW    1
COLNUM: DW      0
LEFTCOL:DW     0
VISCOL: DW      0
TABEND: DW      0

; Physical buffer pointers.  GAPLO/GAPHI delimit unused RAM while GAPOPEN=1.
BUFEND: DW      BUFBEG
CURPTR: DW      BUFBEG
GAPLO:  DW      BUFBEG
GAPHI:  DW      BUFBEG
TOPPTR: DW      BUFBEG
OLDTOP: DW      BUFBEG
DRAWPTR:DW     BUFBEG
LINETMP:DW     0
MOVEST: DW      0
MOVEEND:DW     0
SAVEPTR:DW     0
OUTPTR: DW      0
FINDPTR:DW     0
CANDPTR:DW     0
INPTR:  DW      0
POSPTR: DW      0
GCPTR:  DW      0
OLDLEFT:DW     0
NAVOLD: DW      BUFBEG

; Parser, record-I/O, number-printing, and clipboard counters.
BASECNT:DB      0
EXTCNT: DB      0
PART:   DB      0
PARTCHR:DB      0
INLEN:  DB      0
LOADIDX:DB      0
OUTCNT: DB      0
IOERR:  DB      0
LEAD:   DB      0
CLIPLEN:DB      0

; CP/M FCBs and fixed scratch buffers remain in the program's low-memory area.
FILEFCB:DS      36
NEWFCB: DS      36
TMPFCB: DS      36
DISKBUF:DS      128
INBUF:  DS      32
MSGBUF: DS      80
CLIPBUF:DS      255

        END     START
