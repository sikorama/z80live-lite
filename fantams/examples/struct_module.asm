; Démo STRUCT + MODULE (fantams)
        org 0x8000
        run main

; --- structure ---
STRUCT Sprite
x   db 0
y   db 0
hp  dw 100
ENDSTRUCT

; instanciation d'un sprite en mémoire, avec valeurs
STRUCT Sprite hero 16, 32, 250

; --- module (scope par préfixe) ---
MODULE gfx
@@export main              ; main reste global (accessible par RUN)
main:
        ld a,(hero.x)      ; accès à un champ d'instance
        ld hl,Sprite       ; Sprite = sizeof (=4)
        call clear         ; réf locale au module -> gfx_clear
        ret
clear:
        xor a
        ret
ENDMODULE
