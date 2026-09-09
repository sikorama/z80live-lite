# Context Map

## Contexts

- [z80live](./CONTEXT.md): l'application (SPA Svelte + WASM) qui édite, assemble et exécute des sources Z80
- [fantams](./fantams/CONTEXT.md): l'assembleur/linker Z80 lui-même (submodule, dépôt et vocabulaire propres)

## Relationships

- **z80live → fantams**: z80live invoque fantams en WASM (`--target`, `-T`, `-o`) et consomme ses artefacts (SNA, plus tard les conteneurs DSK/CPR/CRO). Le vocabulaire de sortie (Artefact, Conteneur, Profil, Morceau...) appartient à fantams — z80live ne le redéfinit pas, il le référence.
