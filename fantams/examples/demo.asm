; Démo fantams : macros, REPEAT, IF, scope auto-local + export SNA
LET DEBUG = 1
LET COUNT = 4
        org 0x8000
        run start

; macro avec label local (auto-local -> unique par appel). Convention rasm : seul un
; label préfixé par '@' est rendu unique par expansion ; un label ordinaire (sans '@')
; réutilisé entre deux appels resterait une vraie collision ("symbole déjà défini").
MACRO WAIT n
@wloop: dec {n}
        jr nz,@wloop
ENDM

start:
        ld b,{COUNT}    ; {} = substitution PP (COUNT est une variable préprocesseur)
        WAIT b          ; 1er appel -> @wloop__x
        WAIT b          ; 2e appel  -> @wloop__y (pas de collision)

; table générée par REPEAT (i est 1-based, comme rasm : 1er tour i=1)
tbl:
REPEAT COUNT, i
        db {=i*i}       ; carrés : 1,4,9,16
REND

IF DEBUG
        ld a,0xFF       ; inclus seulement si DEBUG
ELSE
        ld a,0
ENDIF
