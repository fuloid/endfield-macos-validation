# Pre8 versus FineWine's October 20 NOP decoder

Reviewed 2026-10-21. This review leaves both production and experimental NOP
patches unchanged. The separately approved layered-window fix is patch 0020.

## Inputs

- Production: `port/mac-handoff/extracted.c`, identical to the helper extracted
  from pinned Wine 11.19 plus patch 0001.
- FineWine: `ec1a2843aaa24bad8b09b0459633450b2930e72b`,
  `patches/stage1-macos/0001-macos-rosetta-signal-fixes-nop-and-privinstr.patch`.
- CrossOver 26.3's original `handle_cet_nop`, inspected in its source archive.
- Intel SDM, NOP and RDSSPD/RDSSPQ instruction entries. Documented multi-byte
  NOP is `0F 1F /0`. RDSSP is a NOP when CET shadow stacks are disabled; its
  encoding must not be treated as universally side-effect-free with CET enabled.

## Findings

1. The new FineWine bounds checks address its older implementation, not a
   demonstrated bounds bug in our helper. Our helper already validates the
   ModRM/SIB availability, displacement length and complete 15-byte limit before
   advancing RIP. Its 16-byte read uses Wine's fault-tolerant reader. FineWine
   requests 15 bytes instead. Both reject the tested truncated/overlong cases.
2. FineWine handles only `0F 1F /0` in its new helper. Our helper additionally
   accepts nonzero ModRM.reg fields and `F2`/`F3` prefixes. These choices are
   deliberately represented in our existing tests, but passing those tests
   establishes implementation consistency, not architectural correctness.
   The comment claiming all such encodings have no side effects on hardware is
   broader than the documented NOP guarantee. No captured Endfield fault in this
   review establishes a need for the extra encodings.
3. Our helper also decodes `0F 1E`, including ENDBR64 and RDSSP examples.
   FineWine's new NOP helper rejects those, but its signal path first calls
   CrossOver's existing `handle_cet_nop`. Consequently comparing only the two
   NOP helpers understates FineWine's total coverage. Production upstream Wine
   does not inherit that CrossOver handler. A direct replacement would remove
   behavior we currently supply ourselves.
4. Both paths retain the privileged-instruction exception translation needed
   for Rosetta's CR3 behavior. Their context/handler-exit mechanics differ with
   Wine 11.0 versus 11.19. Preserve 11.19's existing `leave_handler` path when
   adapting any instruction-decoding change.
5. FineWine also repairs diagnostic logging that printed bytes beyond the
   successfully read range. Our NOP helper emits only the decoded length; it
   does not contain that logging bug.

## Executed comparison

Compiled the exact two helpers with GCC on the Windows x86_64 host, using a
stubbed Wine memory reader and isolated contexts. Compared opcodes 1E/1F, all
256 ModRM bytes, seven prefix choices (none/66/67/F2/F3/F0/48), and readable
lengths 0 through 16: 60,928 combinations. These are helper tests, not actual
instruction execution and not Rosetta/gameplay evidence.

- Both accepted: 1,492; instruction lengths agreed in every shared case.
- Production only: 34,060; FineWine NOP helper only: 0.
- The observed Endfield `0F 1F C1`: both skip exactly 3 bytes.
- `0F 1F C9`, `F2 0F 1F C1`, `F3 0F 1F C1`: only production accepts.
- `F3 0F 1E FA` (ENDBR64) and `F3 48 0F 1E C8` (RDSSPQ): production
  accepts; FineWine delegates this opcode family to its separate CET handler.
- Truncated SIB+disp32 and LOCK-prefixed NOP examples: both reject.
- Existing production helper suite: 8,735 NOP and 11 XGETBV checks pass.

## Recommendation

Do not transplant the FineWine patch wholesale or describe it as fixing a
known production bounds failure. A future change can separate the documented
`0F 1F /0` decoder from explicit CET compatibility handling, retain Wine 11.19's
signal cleanup, and add independent negative/guard-page tests. Decide how to
handle undocumented encodings using captured faults and native/Rosetta evidence
before narrowing the shipped compatibility behavior. No game-start improvement
is established by this review.

References:

- https://github.com/stoicswe/Endfield_FineWine/commit/ec1a2843aaa24bad8b09b0459633450b2930e72b
- https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- https://cdrdv2-public.intel.com/835781/325462-sdm-vol-1-2abcd-3abcd-4.pdf
