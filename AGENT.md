# Plane Agent Guide

This file records the stable coding rules for this repository. Follow it when
editing code, writing tests, or preparing commits. Do not use `.clang-format`
to rewrite whole files unless explicitly asked; prefer small, manual patches.

## Code Style

- Use Linux brace style: function definition braces go on the next line;
  `if`, `for`, `while`, `switch`, and `else` braces stay on the same line.
- Use tabs for indentation in C, headers, and assembly-adjacent code.
- Keep manual line wrapping intentional. Do not run broad auto-formatting that
  changes hand-tuned widths.
- Use lowercase hex prefixes and suffixes in project code, such as `0x1000ull`.
- Do not place `(void)` before function calls. Keep `(void)arg` only for
  intentionally unused parameters or compile-time expressions.
- Add and use compiler attribute wrappers in `plane/compiler.h`, such as
  `__noreturn`, `__packed`, `__used`, `__weak`, `__aligned(x)`, and
  `__section(name)`, instead of spelling raw `__attribute__((...))` in project
  code.

## Comments

- Prefer short `/* ... */` comments.
- Add comments at module boundaries, public APIs, non-obvious assumptions,
  manual/spec references, and TODO boundaries.
- Do not narrate obvious code. Comments should explain why a boundary exists or
  what assumption the code relies on.
- Keep local style in mind: short notes like `/* in linker_grub.lds.S */` are
  preferred over long prose when enough.

## Architecture Boundaries

- Generic `include/hal/` and `include/plane/` headers must not expose x86_64,
  GRUB, Limine, or Multiboot2 implementation details.
- Architecture-private APIs and symbols should live under `hal/x86_64/` or
  `include/hal/x86_64/` and use an `x86_64_` prefix when they cross files.
- Boot protocol code parses protocol data only. If it needs architecture
  services, use a protocol-local arch hook such as `boot_mb2_arch_*()` instead
  of including `hal/x86_64/...` from `boot/`.
- Keep Multiboot2 early handoff helpers private to the x86_64 Multiboot2
  boundary. They are not generic HAL MMU APIs.
- Test override seams must not leak into production ABI. Prefer
  instantiable objects or ordinary link-time dependency replacement over
  test-only hooks in production implementation files.

## Headers

- Include the most specific header needed by the file.
- `plane/util.h` is for generic utility macros such as `container_of`,
  `ARRAY_SIZE`, and alignment helpers. It must not include `plane/bits.h`.
- Files that need bit operations must explicitly include `plane/bits.h`.
- Keep C-only helpers behind `#ifndef __ASSEMBLER__` in headers shared with
  assembly.
- External protocol headers such as `include/limine.h` and
  `include/multiboot2.h` are treated as imported interfaces; avoid style churn
  there unless a protocol integration change requires it.
- Use named macros for values with architecture, protocol, ABI, or page-table
  meaning. Do not macro-ize ordinary local test values or obvious loop counts
  just to remove every literal.

## Diagnostics

- Use `BUG()`, `BUG_ON()`, and `BUG_ON_MSG()` for unrecoverable kernel
  contract failures, such as corrupted boot handoff data, missing required CPU
  capabilities, or initialization failures where continuing would be unsafe.
- Use `WARN_ON*()` only when the kernel can continue running and the condition
  is still worth recording.
- `BUG_ON*()` and `WARN_ON*()` conditions are evaluated, following Linux
  kernel-style semantics rather than C `assert()`/`NDEBUG` semantics.
- Simple must-succeed boolean calls are acceptable in `BUG_ON_MSG(!fn(...))`.
  Split complex rollback, multi-step actions, multiple results, or unclear
  `WARN_ON*()` side effects into named local variables.
- Library-style helpers should usually return `false` or an error sentinel and
  let the caller decide whether to `BUG_ON_MSG()`.
- Do not use silent `hal_cpu_hang()` from C paths when printk/serial
  diagnostics are available. Keep raw hangs for very early assembly or terminal
  halt paths where diagnostics cannot be trusted.

## MM, PMM, and pmap

- PMM is currently an early, single-core allocator. Do not assume SMP safety,
  locking, or atomic semantics until those are added explicitly.
- `struct plane_page` is the XNU-like page metadata foundation. Do not expand
  it into VM object, pageout, coloring, or per-CPU queue behavior without a
  separate plan.
- Physical allocation APIs return physical addresses, not dereferenceable
  virtual addresses. Use the HAL direct-map API to access page contents.
- Mutating x86_64 pmap root helpers operate only on PMM-owned page-table roots.
  Read-only translation helpers may inspect any direct-map-accessible root.
- Active kernel pmap wrappers are responsible for local TLB invalidation.
  Root-parameter helpers must not invalidate TLB entries.

## Tests

- Use `tests/support/test.h` for test runners, case lists, and common
  assertions.
- Test files should focus on input, call, and expected result. Shared failure
  formatting belongs in test support helpers.
- Keep stubs at the test boundary. Do not move module-specific HAL, PMM, or
  printk stubs into generic support unless multiple tests genuinely need them.
- Tests should instantiate their own state when the production object model
  allows it. Hardware dependencies can be replaced by test stubs at the link
  boundary, but production files should not grow test-only reset branches.
- x86_64 arch-private tests should be named `x86_64_*_test`.
- Tests may include arch-private headers when they explicitly test arch-private
  modules.

## Build and Generated Files

- Do not commit `.config`, `include/generated/autoconf.h`, `build/`, `*.o`,
  `*.d`, `*.elf`, `*.iso`, `tools/limine_bin/limine`, or `.vscode/`.
- Do not manually delete `include/generated/`; Makefile rules are responsible
  for recreating and cleaning generated configuration files.
- Use `make clean` or `make distclean` for build cleanup, and confirm
  `include/generated/` still exists afterward.

## Commit Hygiene

- Stage only the files relevant to the change. Avoid `git add .`.
- Keep commits focused: style cleanup, architecture boundary changes, and
  behavior changes should be separate unless a plan explicitly combines them.
- Before committing code changes, run the relevant host tests and
  `git diff --check`. For boot/MMU/HAL changes, also build both GRUB and Limine
  configurations when practical.
