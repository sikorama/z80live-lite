; Démo rasm-lite : macros, REPEAT, IF, scope auto-local + export SNA
LET DEBUG = 1
LET COUNT = 4
        org 0x8000
        run start

; macro avec label local (auto-local -> unique par appel)
MACRO WAIT n
wloop: dec {n}
       jr nz,wloop
ENDM

start:
        ld b,{COUNT}    ; {} = substitution PP (COUNT est une variable préprocesseur)
        WAIT b          ; 1er appel -> wloop__x
        WAIT b          ; 2e appel  -> wloop__y (pas de collision)

; table générée par REPEAT
tbl:
REPEAT COUNT, i
        db {=i*i}       ; carrés : 0,1,4,9
REND

IF DEBUG
        ld a,0xFF       ; inclus seulement si DEBUG
ELSE
        ld a,0
ENDIF
