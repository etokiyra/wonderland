# PercyLang reuse audit

Audited repository: <https://github.com/etokiyra/percylang>
Commit inspected: `b8b8dc63252e54ec2b82e3782a16c2d943b92ec2`
("docs: tighten the checkpoint wording", 2026-11-12)
Clone: `/tmp/opencode/percylang` (outside this repository, as required)
Audit date: 2026-11-18

Method: read `LICENSE`, `go.mod`, `README.md`, and the source of the lexer,
parser, AST, checker, loader, code generator, the embedded C runtime, and the
test harnesses. Counted files and functions. Did **not** build or run the
project: it is a Go compiler requiring Boehm GC, and running its test suite
would not change any reuse conclusion. Go 1.27.4 and `libgc` are present on
this machine, so the suite could be run later if a specific claim ever needs
it.

## 1. What PercyLang actually is

| Fact | Value |
|------|-------|
| Language of implementation | Go (`module percy`, go 1.27.2) |
| Size | 254 files; 79 `.go` files; 54 `_test.go` files |
| Components | `lexer/`, `parser/`, `checker/`, `codegen/`, `loader/`, `runtime/`, `cmd/purr/`, `selfhost/`, `purr/` |
| Output | native executables via generated C (embedded C runtime in `runtime/csrc/`) |
| Licence | GPL-3.0-or-later for the repository; **MIT** for `runtime/csrc/` |
| Build/test scaffolding | Go toolchain only; no `Makefile`, no C test harness |
| Shell relevance | none directly: it is a statically typed, garbage-collected programming language |

This matters up front: the brief suggests reusing "lexer architecture",
"source positions", "AST conventions", "parser architecture", "testing
infrastructure" and "memory-management patterns". Those are **design**
artefacts. There is no C source in the front end to copy, and the only C code
(the runtime) is built around a garbage collector Wonderland cannot use.

## 2. Verdict summary

| Area | Verdict | Why |
|------|---------|-----|
| Lexer control flow (`peek`/`advance`/`scan`, longest-match operators) | **adapt** | correct shape, but rune-based, fail-fast, no quoting model |
| Token representation (`Kind`, `Text`, `Line`, `Column`) | **redesign** | no byte offsets, no end position, no quoted/unquoted information |
| Kind names table + `String()` fallback | **reuse directly (concept)** | trivial, useful for dumps and diagnostics |
| Source positions | **redesign** | `Position{Line, Column}` only; Wonderland needs byte spans |
| AST node conventions | **adapt** | sealed-interface pattern becomes `enum kind` + union in C |
| Parser architecture (recursive descent, `expect`, `errorAt`) | **adapt** | sound pattern; shell grammar and reserved-word rules are different |
| Precedence climbing (`parseBinary(minPrecedence)`) | **adapt** | not needed for pipelines, useful if arithmetic expressions arrive |
| EOF sentinel synthesis (`eofToken`) | **reuse directly (concept)** | cheap robustness for line-at-a-time input |
| Diagnostics (`Error{Line, Column, Message}`, `ErrorKind`) | **adapt** | extend with file, span, snippet; add recovery for interactive use |
| Loader (canonical paths, load-once set) | **adapt** | good pattern for pack dedupe; shell packs have no includes in v1 |
| C string escaping (`cStringLiteral`) | **adapt** | directly useful technique for quoting/rendering content safely |
| C runtime (`pcy_string`, maps, arrays) | **do not reuse** | Boehm GC, `GC_MALLOC`, value semantics that do not fit a shell |
| Subprocess capture (`pcy_rt_try_run_capture`) | **adapt (as a reference only)** | one-shot fork/pipe/wait; a shell needs job control and process groups |
| Checker, code generator, self-hosted compiler, `purr` CLI | **do not reuse** | unrelated problem domain |
| Test harness style (table-driven, `assertTokens`, `assertLexError`) | **adapt** | shape maps cleanly onto the C harness and golden files |
| Differential testing (stage 1 vs stage 2) | **adapt** | the exact model for dash/bash comparison, including documented divergences |
| Build/test scaffolding | **do not reuse** | there is none for C |

Nothing is reusable as source code. The useful material is four things:
token-kind tables with human names, the lexer/parser control-flow skeleton, the
diagnostic shape, and the *method* of differential testing. Everything else is
either Go-specific or shaped by the wrong memory model.

## 3. Lexer

### Inspected

* `lexer/token.go:11-107` — `Kind` int enum; every keyword, operator and
  punctuation mark has a distinct kind, so the parser never re-inspects text.
* `lexer/token.go:111-193` — `kindNames map[Kind]string` and
  `Kind.String()` with a `Kind(%d)` fallback.
* `lexer/token.go:196-211` — `Token{Kind, Text, Line, Column}`; `Text` is the
  exact source spelling, including quotes and escapes.
* `lexer/lexer.go:54-80` — `Lex(source string) ([]Token, error)`: scans the
  whole input, always appends one `EOF`, returns the first error and `nil`
  tokens on failure.
* `lexer/lexer.go:83-123` — `scan()` + `skipTrivia()`; `lexer` holds
  `source []rune`, `pos`, `line`, `column`.
* `lexer/lexer.go:290-410` — `scanOperator` (longest match), plus
  `peek`/`peekAt`/`advance`/`atEnd`.
* `lexer/string.go:14-40` — `DecodeString`, decoding `\n \t \" \\`.

### Assessment

The control-flow skeleton is the right one and is cheap to reimplement: a
cursor over a byte buffer, explicit `peek`/`peek_at`/`advance`, longest-match
operator scanning, one EOF sentinel, positions advanced in one place. Wonderland
should keep that shape.

The token model is not usable. `Line`/`Column` are the only positions
(`lexer/token.go:205-210`), so there is no way to slice the original source,
which a shell needs for here-document bodies and for exact caret rendering.
There is also no notion of quoting: `Token.Text` is "the original source
spelling" (`token.go:199-203`), which is correct for a language with only
double-quoted literals and wrong for a shell, where `a|b` and `a"|"b` are
fundamentally different and expansion must know which bytes were quoted.

Fail-fast lexing is fine for a compiler invoked on a file; a shell must also
survive a half-typed line at a prompt, so the lexer's error path must not
throw away the interactive session (it may discard the line).

### What Wonderland takes

* Byte buffer + explicit cursor helpers, no regex, no re-scan from the start.
* Longest-match operator table (`<<-` before `<<`, `>>` before `>`).
* A `Kind` enum plus a name table for `wl_ast_dump()` and diagnostics.
* One `TOK_EOF` appended at the end, with the position one past the last byte.

### What Wonderland does not take

`[]rune` (byte-oriented shell), the `Token` struct, per-token `Text` ownership,
the single-error return contract, and the keyword map (shell reserved words are
position-sensitive and cannot be lexed by lookup alone — see
`docs/GRAMMAR.md` §2.1).

## 4. Positions, spans, diagnostics

### Inspected

* `parser/ast.go:15-19` — `type Position struct { Line, Column int }`.
* `parser/ast.go:21-34` — `Node` interface with `Pos() Position`; `Decl`,
  `Stmt`, `Expr` extend it with unexported marker methods.
* `parser/parser.go:114-134` — `errorAt`, `describe`, `positionOf`.
* `parser/parser.go:9-29` — `Error{Line, Column, Message}` with
  `line:column: message`.
* `checker/checker.go:11-79` — `ErrorKind` enum with a `String()` switch.
* `checker/checker.go:91-112` — `Error{Kind, File, Line, Column, Message}`;
  `Error()` renders `file:line:col: message` when a file is known.
* `checker/checker.go:210-218` — `Check` returns a single `error`, no batch.

### Assessment

Two good ideas: (a) typed error kinds so callers can distinguish problems
without matching message text (`checker.go:11-19`), and (b) a stable
`[file:]line:col: message` format. Both should survive into Wonderland.

Two gaps: positions are line/column only, and diagnostics are fail-fast with
no source excerpt. A shell's parse errors are short and user-generated, and a
caret line is worth the ~40 lines it costs. Wonderland's span is
`{byte start, byte end, line, col}` (`docs/GRAMMAR.md` §4).

There is also no snippet rendering anywhere in the repository; do not go
looking for it.

### What Wonderland takes

* `ErrorKind` + `String()` with an explicit default case.
* The `file:line:col: message` convention (extended with a caret block).
* Fail-fast per parse *unit* — where a shell's unit is one command line.

## 5. Parser and AST conventions

### Inspected

* `parser/parser.go:32-52` — `Parse`/`ParseSource` entry points.
* `parser/parser.go:54-72` — `eofToken`: synthesises an EOF token when the
  stream is empty or truncated, so `peek` never indexes out of range and error
  positions stay accurate. This is a solid, cheap robustness trick.
* `parser/parser.go:74-134` — `peek`, `peekAt`, `advance`, `expect`,
  `errorAt`: the whole parser interface is six functions.
* `parser/parser.go:932-1020` — `binaryPrecedence`, `isRightAssociative`,
  `parseExpression`, `parseBinary(minPrecedence)`: textbook precedence
  climbing.
* `parser/ast.go:21-34, 60-200` — nodes carry an `At Position`, implement
  `Pos()`, and mark their category with unexported methods (`declNode()`,
  `stmtNode()`, `exprNode()`), sealing the interface set to the package.
* `parser/parser.go` throughout — explicit nil checks when walking child
  nodes; the package documents that `Parse` never panics on malformed input.

### Assessment

The recursive-descent skeleton transfers almost unchanged in spirit: a parser
struct with a token array, index, and helpers; functions named after grammar
productions; `expect` producing positioned errors. The sealed-interface trick
is a Go idiom that C cannot express; the C equivalent is `enum wl_node_kind`
plus a `union`, which is also easier to switch on and easier to dump.

Precedence climbing is unnecessary for pipelines and lists (which are just
left-associative chains), but it is the right approach for arithmetic if
`$((...))` gets an evaluator in M3. Keep the technique in mind rather than
copying the code.

Node allocation in PercyLang is ordinary Go heap allocation with GC. That is
precisely what Wonderland's arena model replaces (`docs/ARCHITECTURE.md` §5).

### What Wonderland takes

* `peek`/`peek_at`/`advance`/`expect` with positioned errors.
* EOF synthesis for truncated streams.
* The discipline of nil/none checks in every traversal.
* Precedence climbing, *if and when* arithmetic evaluation is written.

### What Wonderland redesigns

* AST shape: `enum kind` + union, span instead of `Position`, words as part
  lists (`docs/GRAMMAR.md` §4) — the shell-specific core has no analogue in
  PercyLang at all.
* Error recovery: one failed line must not kill an interactive session.
* Reserved-word classification: position-sensitive, not a keyword table.
* Memory: per-line arena, no GC.

## 6. Loader

### Inspected

* `loader/loader.go:3-21` — package doc: load root module + imports, resolve
  relative paths, canonicalise to absolute, load each module once, reject paths
  outside the root directory.
* `loader/loader.go:44-62` — `Load(rootPath) ([]*parser.Module, error)`;
  depth-first order, `ID` = index.
* `loader/loader.go:128-148` — `canonicalPath`, `withinDirectory`.
* `loader/loader.go:150-158` — `wrapFrontendError` attaches the module path to
  a lexer/parser error.

### Assessment

Wonderland's pack loader is much smaller (no imports in v1) but the same three
habits apply: canonicalise paths, key a "seen" set by canonical path so
duplicates and cycles terminate, and wrap inner errors with the file name.
`withinDirectory` is worth remembering for a future pack `include` feature,
but adding includes now would create exactly the class of bug this code had to
solve. Do not add them (D-0021 / `docs/CONTENT_FORMAT.md` §1).

## 7. The C runtime (`runtime/csrc/`)

### Inspected

* `runtime/csrc/LICENSE` — MIT, "Copyright (c) 2026 etokiyra".
* `runtime/csrc/percy_runtime.h:98-101` — `pcy_string { int64_t length;
  const char *data; }`.
* `runtime/csrc/percy_runtime.c:89-95` — `pcy_rt_init` calls `GC_INIT()`;
  `pcy_rt_alloc` returns `GC_MALLOC(size)`.
* `runtime/csrc/percy_runtime.c:154-218` — string equal/concat/substring.
* `runtime/csrc/percy_runtime.c:367-383` — FNV-1a over bytes.
* `runtime/csrc/percy_runtime.c:385-530` — open-addressing, linear-probing map
  with power-of-two capacity, bounded probe loop, no deletions.
* `runtime/csrc/percy_runtime.c:605-700+` — `pcy_rt_try_read_file`,
  `pcy_rt_try_run` (fork/exec/wait), path and errno formatting.
* `runtime/csrc/percy_runtime.c:787-1140` — capture buffers, pipes,
  `pcy_rt_try_run_capture`, `pcy_rt_wait_capture`.

### Assessment

This is the only C code in the project, and it is genuinely well-commented.
Three conclusions:

1. **Licensing is favourable for these files specifically** (MIT), but the
   repository as a whole is GPL-3.0-or-later. Copying anything outside
   `runtime/csrc/` would force GPL on Wonderland; copying from `runtime/csrc/`
   is permitted with the MIT notice preserved.
2. **The code is not usable as-is** because it allocates through Boehm GC
   (`GC_MALLOC`), which violates Wonderland's libc-only, explicit-ownership
   policy (D-0012, D-0019). `pcy_string` is immutable and GC-owned; shell
   strings need arena or caller ownership and in-place mutation during
   expansion.
3. **The techniques are worth reading**: length-carrying strings (already the
   plan), FNV-1a for any content-pack hashing, and the bounded linear probe
   (the loop is capped by capacity so a corrupted table cannot hang — a good
   habit for the pack loader's duplicate detection).

The subprocess code (`try_run_capture`) is the closest thing to shell
execution in the repository, and it is a warning rather than a model: it is a
single-shot capture with no process groups, no terminal ownership, no job
table, no signal discipline. A shell needs all of those. Reading it is useful
for the `pipe`/`fork`/`waitpid` bookkeeping; copying it would import a design
that cannot do job control.

### What Wonderland takes

* Length-carrying string decisions (independently reimplemented).
* FNV-1a as the content-pack and identifier hash, if one is needed.
* Bounded probing / defensive loop caps.
* Error-message style: `action "path": reason`, terse and specific.

### What Wonderland does not take

* `GC_*` allocation, `pcy_string`, `pcy_array`, `pcy_map` as code.
* `pcy_rt_try_run*` (redesign around job control from the start).
* The generated-C pipeline, the ABI constant tables, `runtime.go` embedding.

## 8. Code generation (`codegen/`) — one narrow exception

`codegen/cstring.go:21-68` (`cStringLiteral`) is the one piece of code whose
*algorithm* transfers directly, even though the function is Go. It decodes a
Percy literal and re-encodes it as a safe C string: quotes and backslashes
escaped, `\n \t \r` named, `?` escaped to avoid trigraphs, non-printables as
fixed-width octal.

Wonderland has the same problem in two places: emitting test goldens safely
and rendering pack text into a terminal without letting content inject control
characters. The validation rule that rejects `ESC` in packs
(`docs/CONTENT_FORMAT.md` §5 rule 10) covers the security side; the escaping
technique is useful for the diagnostic/dump side. Reimplement it in C in ~30
lines when needed; do not copy the Go.

The rest of `codegen/` (statements, expressions, struct layout) is unrelated
to shells and should not be read for inspiration.

## 9. Testing infrastructure

### Inspected

* `lexer/lexer_test.go:12-50` — a `token(kind, text, line, column)`
  constructor and `assertTokens` / `assertLexError` helpers that compare whole
  token slices and assert error positions.
* 54 `_test.go` files, concentrated in `cmd/purr/` (~40), with unit tests per
  package.
* `cmd/purr/differential_test.go:1-40` — the pattern: run two independent
  implementations on the same corpus, compare exactly, and *document the known
  divergences in the file header* rather than hiding them behind loose
  assertions. It also normalises one known difference (rune vs byte columns)
  explicitly.
* `checker/checker_test.go` — table-driven success/error cases with
  `contains` assertions on messages and exact error-position checks.

### Assessment

This is the most valuable thing in the repository for Wonderland, and none of
it is code to copy. Two practices:

1. **Whole-output comparison with exact positions**, not "contains some
   substring". A token test that accepts a partial match is worthless.
2. **Differential testing with a written list of accepted differences.** That
   is exactly how `docs/COMPATIBILITY.md` §5-§6 is designed: compare against
   dash/bash, and put every intentional divergence in the document and the
   runner.

Wonderland's harness is deliberately smaller (`tests/unit/test.h`, ~130 lines)
because it must be dependency-free; the conventions map cleanly: `assertTokens`
becomes a golden dump comparison, `assertLexError` becomes an assertion on
`wl_diag` position and kind.

## 10. Licensing

| Path | Licence | Consequence if copied |
|------|---------|----------------------|
| Repository root, all `.go` | GPL-3.0-or-later (`LICENSE`, README badge) | Wonderland would have to be GPL-3.0-or-later; incompatible with an MIT/Apache decision unless the whole project relicenses |
| `runtime/csrc/` | MIT (`runtime/csrc/LICENSE`) | copyable with the copyright/permission notice retained |
| `go.mod`, docs, `SPEC.md` | under the root GPL | copying prose would still be a GPL derivative work; write our own |

Boehm GC itself is MIT/X11-like, but linking it is forbidden by the
libc-only policy regardless of licence.

**Recommendation:** copy no code. The design takeaways above are not
copyrightable expression in any meaningful sense when independently
reimplemented, and the licensing question (OQ-1) stays open. If a specific C
file from `runtime/csrc/` is ever copied, record it in `docs/DECISIONS.md`
with the MIT notice retained in the file header, and note the divergence from
libc-only in the same entry.

One caution for the audit trail: the *comments* in PercyLang are long and
distinctive. Do not paraphrase them into Wonderland's sources; write our own
rationale tied to our own decisions.

## 11. Net recommendations

1. Reimplement the lexer/parser skeleton in C from scratch; keep the
   `peek`/`advance`/`expect` shape and the EOF sentinel trick.
2. Keep byte spans, not `Position`, and keep a per-byte quote map; the shell
   word model has no counterpart in PercyLang, so do not look for one.
3. Copy the diagnostic *shape* (`kind`, `file`, `line`, `col`, message,
   `kind.String()`), not the fail-fast-only policy.
4. Use the differential-testing method, including the "known divergences"
   discipline, against dash and bash (`docs/COMPATIBILITY.md`).
5. Do not import the GC runtime, the checker, the code generator, or the `purr`
   CLI. There is no shell in PercyLang; treat it as a source of engineering
   habits, not components.
6. Keep the licence question open and record any future code copying
   explicitly (OQ-1, D-0030).

## 12. Claims this audit does not make

* PercyLang was not built or tested here; no statement about its correctness
  or test status is implied.
* "Reusable" in this document means "the idea transfers", never "the code
  compiles in Wonderland". No code was copied.
* No performance comparison was made; none is relevant at this stage.
