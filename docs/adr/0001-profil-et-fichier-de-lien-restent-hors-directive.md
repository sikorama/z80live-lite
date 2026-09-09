# Le Profil et le Fichier de lien restent hors de la Directive de build

Aujourd'hui, `assembler`/`buildmode`/`entry` sont dupliqués dans la Directive
de build en tête de Source, pour qu'une Source exportée seule reste
assemblable sans la base. Le Profil (`cpc6128`/`cpcplus`) et le Fichier de
lien (`.ld`) n'y seront **pas** ajoutés : ce mécanisme de commentaire est jugé
peu convaincant et on ne veut pas en généraliser l'usage, et un chemin de
Fichier de lien écrit en commentaire ne serait de toute façon pas exploitable
tel quel. Ces deux réglages restent uniquement en base, associés au Projet. Un
futur export de Projet en archive (Fichier de lien à côté des Sources) est
envisagé comme meilleure réponse au besoin de portabilité.
