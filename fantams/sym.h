// sym.h - Table des symboles exportable (ADR 0019)
//
// Rend l'`asmb::Output` d'un assemblage en un CSV d'une ligne par symbole,
// destine a un desassembleur ou a un emulateur — l'equivalent de ce que rasm
// ecrit avec son `.sym`, plus le type et la PROVENANCE.
//
// Pas un LISTING : un listing donne banque, adresse et octets ligne de source par
// ligne de source (cf. CONTEXT.md). Ici il y a une ligne par NOM, et aucun octet.
//
// Fonction PURE : elle prend une structure, elle rend une chaine. L'ecriture du
// fichier appartient au CLI — c'est ce qui rend le format testable sans toucher au
// disque, et exposable au WASM sans le reecrire.
#pragma once

#include "asm.h"

#include <string>

namespace sym {

// Le CSV complet, en-tete comprise, terminee par un '\n'.
//
// Colonnes : name,type,value,bank,store,file,line
//
//   name   le nom tel que l'assembleur le connait : QUALIFIE (« plot.loop ») et
//          MANGLE (« retry__7 »). C'est le nom qui correspond reellement a
//          l'adresse, seul utilisable par un consommateur.
//   type   « label » ou « const ». Les variables ('=') ne sont pas exportees.
//   value  l'adresse LOGIQUE d'un label, la valeur d'une constante. Hexa 0x de
//          largeur variable, signe pour une constante negative.
//   bank   la banque de RANGEMENT, en decimal. « - » pour une constante.
//   store  l'adresse de RANGEMENT, hexa. « - » pour une constante. Elle ne differe
//          de `value` que dans un bloc « org <logique>,<rangement> ».
//   file   le fichier D'ORIGINE, avant preprocesseur.
//   line   la ligne dans ce fichier. Plusieurs symboles peuvent la partager :
//          trois expansions d'une meme macro donnent trois noms mangles et une
//          seule ligne d'origine.
//
// Tri : banque, puis adresse de rangement, puis nom. Les constantes, qui n'ont ni
// l'une ni l'autre, forment la QUEUE du fichier, triees par nom — un consommateur
// qui lit jusqu'a la premiere banque « - » a tous les symboles adressables.
//
// L'en-tete est une VRAIE ligne CSV, non commentee : les noms de colonnes SONT le
// numero de version. Un lecteur qui voit apparaitre une colonne le sait, et
// `csv.reader` n'a pas de preambule a sauter.
std::string format(const asmb::Output &o);

} // namespace sym
