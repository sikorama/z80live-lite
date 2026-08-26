# The fantams syntax

This document describes what fantams accepts **today**, verified against the
binary and not against intentions. Standard Z80 mnemonics are not included here:
they are the same as everyone else's. What follows covers directives, expressions,
macros, and extended notations.

The **decisions** behind these choices live in `docs/adr/`. Here, only the facts.

---

## 1. The line

```
label:  instruction operands   ; comment
```

- The **colon** of a label is optional, but its absence is flagged.
  A label starts in column 1; the rest is indented (`--beautify` establishes
  this form).
- **Comments** run from `;` or `//` to the end of the line, and `/* … */`
  can span multiple lines.
- The **colon also separates instructions**: `ld a,1:inc a` contains
  two. This is a writing convenience, canonicalized to two lines by the
  preprocessor, and refused by `--strict`.
- A reserved word before that colon is **never** a label: `nop:nop:nop`
  assembles to three `nop`, because a mnemonic cannot name a label (ADR 0015).
  It is flagged all the same — `nop:` *reads* like a label — so write
  `nop : nop : nop` to say plainly what is meant.
- A `label:` attached to its instruction is detached by `--beautify`
  (`--no-detach-labels` disables it).

---

## 2. Numbers

| Notation | Base | Example |
|---|---|---|
| `123` | 10 | |
| `#FF` `$FF` `0xFF` | 16 | |
| `%1010` | 2 | |
| `3.14` | 10, float | the dot only makes sense in base 10 |

All values are **reals**; bitwise operators convert to integer (ADR 0008). `$` alone
is the current address — `$FF` remains a number, the distinction is made by what
follows.

---

## 3. Strings

`'text'` and `"text"` are **two notations for the same object**: delimiters
are interchangeable, and `'A'` is in no way different from `"A"` (ADR 0010).

- A string of **one byte** has a value in an expression: the code of its
  character. Any other length has none.
- **Offset string**: `db 'hello'-'a'` applies the arithmetic tail to each
  byte and emits five. This is an **emission** construct, valid where a
  sequence of bytes is expected, never where a value is.
- A `;` in a string does not open a comment.

---

## 4. Expressions

### Operators, from least to most binding

| Level | Operators |
|---|---|
| logical or | `\|\|` |
| logical and | `&&` |
| bitwise or | `\|` `or` |
| bitwise xor | `^` `xor` |
| bitwise and | `&` `and` |
| equality | `==` `!=` |
| comparison | `<` `<=` `>` `>=` |
| shift | `<<` `shl` · `>>` `shr` |
| addition | `+` `-` |
| multiplication | `*` · `/` · `%` `mod` · `div` |
| unary | `-` `+` `~` `not` `!` |
| power | `**` — right-associative, more binding than unary |

- **`/` is floating-point division**, `div` is integer division. `7/2` equals 3.5,
  `7 div 2` equals 3.
- Text aliases designate **bitwise** forms. There is deliberately
  no text alias for `&&`, `||`, and `!` (ADR 0008).
- `//` is **not** an operator: it is a comment.

### Functions

| Function | Arguments | Note |
|---|---|---|
| `sin` `cos` | 1 | angles in **radians** (diverges from rasm, ADR 0021) |
| `abs` | 1 | |
| `hi` `lo` | 1 | high / low byte of the integer value |
| `floor` `ceil` `int` `round` | 1 | toward −∞ / +∞ / zero / nearest |
| `min` `max` | 2 | |
| `sizeof` | 1 | size of a `struct` |

Rounding departs from zero on halves, a deliberate divergence from rasm
(ADR 0009).

`sin` and `cos` take **radians**, another deliberate divergence: rasm takes
degrees (ADR 0021). Write `sin(a*3.14159265/180)` for a degree argument.

### What doesn't exist

There is **no ternary `? :`**, and there won't be: the `:` is already the
instruction separator as much as it is a label suffix, so
`1 ? 2 : 3` is split in two before reaching the evaluator. An `if` says the same
thing more clearly (ADR 0008, amended).

---

## 5. Names and values

| Form | Object | Resolved |
|---|---|---|
| `name:` or `name` at line start | **label** — an address | assembly time |
| `name EQU value` | **constant**, non-reassignable | assembly |
| `name = value` | **variable**, reassignable, sequential | assembly |
| `LET name = value` | **preprocessor variable**, resolution **required** at PP time | preprocessor |

A definition (`EQU`, `=`) never receives a colon: that is its canonical form.

### Local labels

A label starting with `.` belongs to the last global label encountered:

```
plot:
.x      ld a,0
        ld (plot.x+1),a     ; reference from outside
```

A **definition** interleaved (`delta equ 4`) does not change the owner.

A label coming out of a **macro expansion** is a global label like any other, so it
becomes the owner of the `.locals` that follow it: after `poke(…)` whose body
defines `@retry`, a `.local` is qualified as `@retry__2.local`. The symbol table
(`--sym`) is where this becomes visible.

### Reserved words

Registers, pairs, and conditions (`a`, `hl`, `i`, `p`, `nz`, `pc`…) cannot
name a label, a macro parameter, or a loop index. The refusal names what
the word is (ADR 0015) — hence the failure of `for i = …` or a
parameter named `p`.

---

## 6. Emission directives

| Directive | Alias | Effect |
|---|---|---|
| `db` | `defb` `dm` `defm` | bytes, strings, offset strings |
| `dw` | `defw` | 16-bit words (little-endian) |
| `ds` | `defs` `rmb` | reserve `n` bytes with value `v` |

`ds` accepts **multiple pairs** on one line: `ds 3,1,3,2` reserves three
bytes with value 1 then three with value 2.

## 7. Placement

| Directive | Effect |
|---|---|
| `org [b<n>:]address` | sets the assembly address and storage bank |
| `org logical,[b<n>:]storage` | assembles for one address, stores at another |
| `align n` | aligns to a multiple of `n` |
| `run address` | entry point |

### Displaced blocks

`org #A600,#100` assembles for `#A600` — labels take that value — and **stores** the
bytes at `#100`. It is code meant to be **copied** to its logical address before it
runs; a loader elsewhere does the copying (ADR 0005).

- `run` takes the **logical** address, because `run label` must equal `label`. The
  `PC` therefore lands on memory nothing has loaded yet: this **warns**.
- `align` aligns the **logical** address, the one the code will run at. The storage
  address shifts by the same amount and is not aligned.
- The displacement is **not persistent**: a bare `org` resets it.
- `loadAddress` and the binary's extent describe the **storage** address, so a raw
  binary no longer loads at the address of its labels.
- The bank prefix qualifies the **storage** address, so it goes on the **last**
  parameter: `org #4000,b4:#100`. On the first parameter of a two-parameter form it
  is **refused**, naming the replacement.

### Banks

`org b4:#4000` stores bytes in **bank 4**; `#4000` remains the **logical
address**, the one labels take. The offset within the bank equals
`address & 0x3FFF`.

Nothing is deduced: a bank has no natural slot, the gate array RAM
configurations paging any extra bank into slot 1 (ADR 0005).

- Banks **0 to 3** form the base 64 K; without a prefix, the bank follows
  the address (`#8000` is in bank 2).
- The bank is **persistent** from one `org` to the next. A bare `org` that inherits a
  bank outside the base 64 K **warns** — a forgotten prefix would move the block
  without any diagnosis.
- Masking has an assumed cost: `b4:#0000`, `b4:#4000`, `b4:#8000`, and `b4:#C000`
  store in the **same place**. A typo in the slot shows up as
  an overlap, which the assembler reports.

The `.sna` exported carries **64 KB** if the source stays within banks 0-3, and
**128 KB** as soon as it writes to banks 4-7 — a flat dump in both cases, read by
anything that reads an ordinary `.sna`. Beyond **bank 7**, assembly
works but export refuses, naming the banks: it would need the `MEM` chunks
of v3.

A **raw binary** (`-o x.bin`) cannot carry banks — it is a
contiguous interval of logical addresses. A banked source exported this way receives
a warning.

`R:` is reserved for cartridge ROMs, without being implemented.

## 8. Diagnostics in the source

| Directive | Effect |
|---|---|
| `assert condition[,"message"]` | fails in pass 2 if the condition is false |
| `print value[,…]` | displays at assembly |

`assert` expects a comparison operator, so `==` and not `=` (`=` is an
assignment). `print` **swallows expression errors** and displays `0` — a known
flaw, not a rule.

---

## 9. Macros

### Definition

```
macro name p1,p2      |   macro name(p1,p2)      |   name MACRO p1,p2
    …                 |       …                 |       …
endmacro              |   endmacro              |   endmacro
```

Closures: `endmacro` (canonical), `endm`, `mend`, or `end`.

The third notation, `name MACRO p,q`, is **inherited from rasm and warns** once per macro;
`--beautify` rewrites it to `macro name p,q`.

### Call

```
name arg1,arg2        bare form, inherited — warns once per macro
name(arg1,arg2)       parenthesized form (ADR 0018)
name()                without argument
```

The parenthesis is **attached** to the name and the closing one is the **last** character.
This is the only form that reads without knowing the macros — hence the only one worth
using for a macro from an `include`, or not yet written. A first argument in parentheses
is written `name((4),12)`.

`--beautify` adds parentheses to macros it knows.

### Arguments

The **bare** form is the **value**, captured at the call site. **Braces**
are the **text**, which the assembler resolves at the emission point. This is a call
by value against a call by name, and the difference is observable (ADR 0014):

```
n = 5
macro m x
    repeat 3
        db x        ; bare      -> 5, 5, 5
        db {x}      ; braces    -> 5, 4, 3
        n = n - 1
    endrepeat
endmacro
    m(n)
```

The argument is **evaluated**, never substituted textually: `m(1+1)` in a body
doing `db x*2` gives 4, and not `1+1*2` — which is 3.

**When the value doesn't exist**, the bare form falls back to text:

| The argument is… | Fallback | Warning |
|---|---|---|
| a register, `(ix+2)`, a string of more than one byte | text | **no** — `push reg` is an idiom |
| an expression depending on a label (`buffer+2`) | text | **yes** — the fallback moves the moment of resolution |

The warning is anchored on the line of the **body**, where the correction is written, and
names the **call site**, where the fact comes from. It only appears once per
(body line, call site) pair: a call in a loop does not warn each time.

A string of **one byte** has a value (ADR 0010): `m('A')` passes 65.

Braces are never a format prefix (ADR 0011).

### Scope

A label prefixed with **`@`** is made **unique to each expansion** — the rasm
convention. A label without the prefix is **not renamed**: reused across two
expansions, it stays a real collision, and the assembler says so
(`duplicate symbol`).

```
macro poke addr,val
    ld a,val
@retry:                 ; -> @retry__1, @retry__2, … one per expansion
    ld (addr),a
    jr nz,@retry
endmacro
```

The rule is the same for the iterations of `repeat` and `while`. `module`, on the
other hand, renames **every** label of its body by prefixing it (`M.plain`).

`@@export name` takes a label out of the renaming, so that every expansion shares
one name. It therefore only concerns `@`-prefixed labels — on a plain label it has
nothing to exempt:

```
macro m
@@export @glob
@glob:  nop            ; -> @glob in every expansion, hence a shared name
endmacro
```

---

## 10. Blocks

| Opener | Closures | Note |
|---|---|---|
| `if` `ifdef` `ifndef` | `endif` | `else`, `elseif` |
| `repeat n[,var]` | `endrepeat` `rend` | the index starts at **0** |
| `while cond` | `endwhile` `wend` | |
| `for var = low to high` | `endfor` | `until` for an exclusive bound |
| `macro` | `endmacro` `endm` `mend` | |
| `struct name` | `endstruct` `ends` | `sizeof(name)` |

**`end` closes any block**, the innermost one. A named closure that does not
match is an error stating so. Any block `X` closes with `endX` or
with `end`, without exception — `endr` does not exist.

The `repeat` index starting at 0 is the most silent rasm divergence in the
project (rasm counts from 1); it is made explicit in use
(ADR 0016).

A block can open and close on one line: `repeat 3 : dw a,b : rend`.

### Modules

`MODULE name` **switches** the active module — it does not nest. `MODULE`,
`MODULE OFF`, and `ENDMODULE` disable it. Labels in it are prefixed:
`gfx.plot`.

---

## 11. Extended notations

### One-to-many — canonicalized by the preprocessor

The unrolled source shows one instruction per line, so these are expanded
before the assembler sees them (ADR 0017).

| Notation | Equals |
|---|---|
| `push hl,de` · `pop af,bc` | one `push`/`pop` per register |
| `ld a,1:inc a` | two lines |
| `ld de,hl` | `ld d,h` · `ld e,l` |
| `ld hl,(ix+2)` | `ld h,(ix+3)` · `ld l,(ix+2)` |
| `ld (iy-1),de` | `ld (iy+0),d` · `ld (iy-1),e` |

`ld rr,rr'` works across **BC, DE, HL, IX, IY** in both directions, the index
halves being the undocumented `hx`/`lx`/`hy`/`ly`. `ld hl,ix` and `ld ix,iy`
do **not** exist — the DD prefix makes `h` the half of IX, so no instruction
names H and IXH at once. Neither does `ld hl,sp`.

`ld rr,(ix+d)` and `ld (ix+d),rr` cover **BC, DE, HL**. The low byte sits at the
low address, so the **high** half takes `d+1` — the detail one writes backwards
half the time by hand.

### One-for-one — orthographies

The assembler tolerates them; `--normalize` rewrites them; `--strict` refuses
them; `--beautify` leaves them alone (it formats, it does not canonicalize).

| Written | Canon |
|---|---|
| `ld pc,hl` · `jp hl` (idem `ix`, `iy`) | `jp (hl)` |
| `ex hl,de` | `ex de,hl` |
| `ex hl,(sp)` · `ex ix,(sp)` | `ex (sp),hl` · `ex (sp),ix` |
| `ex af,af` | `ex af,af'` — **warned** |
| `defb` `dm` `defm` → `db` · `defw` → `dw` · `defs` `rmb` → `ds` | |
| `endm` `mend` → `endmacro` · `rend` → `endrepeat` · `wend` → `endwhile` · `ends` → `endstruct` | |

`ex af,af` is the **only** tolerance that warns. Its literal reading denotes a
*different* operation — exchanging AF with itself, which is a no-op — where every
other one is merely unfashionable. The rule generalizes: fantams warns when the
text lies, not when it is out of style (ADR 0020).

`jp (hl)` remains the **canon**, despite parentheses suggesting a
non-existent indirection: `ld pc,hl` is non-standard Z80 for everyone, and a
canon that other assemblers refuse would lose what makes its value.

### Repetition

A mnemonic that takes **no operand** may carry a count: `nop 32`, `ldi 16`,
`halt 2`. It is shorthand for `repeat n : <mnemonic> : endrepeat`, and it follows
that reading exactly.

- This is **unrolling**, not canonicalization: `-E` expands it fully — all
  1024 lines of `nop 1024` — while `--normalize`, which canonicalizes *without*
  unrolling, leaves `nop 32` intact. `--strict` refuses it.
- The count is a **preprocessor value**: a variable or an expression is fine,
  an expression touching a **label** is not — at preprocessor time no address
  exists. To reserve space measured on labels defined *above*, use `ds`.
- `nop 0` is legal and emits nothing; a negative count is an error.
- The rule covers **every** operand-less mnemonic, which is broader than rasm
  (it hand-codes ten, so `cpi 4` and `ldir 2` are errors there). `ret` and `im`
  are not operand-less, so a count on them is not a count.

### What is refused, though rasm accepts it

| Form | Why |
|---|---|
| `ld hl,sp` | rasm makes it `ld hl,0 : add hl,sp` — 4 bytes, and the carry is clobbered |
| `rlc hl` · `rr de` · `srl8 de` | 2 to 4 `cb` operations: a routine, not an orthography — write a macro |
| `rst z,#38` | 2 bytes that **overlap** — the `jr` displacement is itself the `rst` opcode — so no pair of canonical Z80 lines expresses it |
| `inc hl,de` · `dec bc,de` | no idiom behind it, and it collides with `add hl,de`; multi-register lists stay on `push`/`pop`, which are a sequence by nature |

`add a,b` and `add b` are **both** accepted (as are `and a,b`, `or a,b`,
`xor a,b`, which rasm refuses). Neither is elected canon: both are one opcode in a
standard spelling, so the difference is a taste, not a structure. `--normalize`
leaves them, `--strict` takes both.

---

## 12. Inclusion

`include "file"` inserts a source. `incbin` and `read` are **reserved but not
implemented** — using them produces a message about a reserved word, not
about a lack.

---

## 13. What is recognized to be refused

fantams keeps these reserved words rather than reading them as labels, to
fail by naming the replacement:

| Word | What the refusal says |
|---|---|
| `BANK` | write `org b<n>:<address>` — bank and address go on the same line |
| `SNASET` `SETCPC` | describes the OUTPUT format, not the program: pass it to invocation |
| `CHARSET` | a character set permutation is an asset encoding: generate the `db` with a script |
| `TICKER` | counting cycles is a control flow analysis, not a directive |
| `STR` | not yet implemented: use `db` (`STR` sets bit 7 of the last character) |

`BUILDSNA`, `BANKSET`, `NOLIST`, and `LIST` are **accepted and ignored**: they are
rasm headers with no effect here.

---

## 14. Assumed divergences from rasm

| Point | rasm | fantams |
|---|---|---|
| `repeat` index | starts at 1 | starts at **0** (ADR 0016) |
| rounding of halves | toward up | **away from zero** (ADR 0009) |
| module syntax | — | assumed divergence |
| macro call | bare | bare **or parenthesized** (ADR 0018) |
| `endr` | absent | absent |

---

## 15. The tool's modes

| Option | Effect |
|---|---|
| `-E` | writes the **unrolled source**: macros expanded, loops unrolled, canonicalized |
| `--normalize` | canonicalizes **without** unrolling |
| `--beautify` | formats only (labels in column 1, blocks indented, call parentheses) |
| `--strict` | refuses anything not canonical Z80 |
| `--no-detach-labels` | keeps `label: instruction` on one line |
| `--no-indent-blocks` | does not indent block bodies |
| `--sym[=file]` | writes the **symbol table** (CSV) for a disassembler or emulator |

`--sym` writes one line per **label and constant** — name, type, logical value,
storage bank and address, origin file and line (ADR 0019). Not a listing: one line
per *name*, and no bytes. Variables (`=`) are left out. The default path derives
from `-o`, so the file travels next to the binary it describes. It refuses to
combine with `--beautify` and `--normalize`, which never reach the assembler, and
cohabits with `-E`. For a human reading a terminal, `-s` prints the table instead.
