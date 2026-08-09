# Plane Agent Guide

This file records the stable coding rules for this repository. Follow it when
editing code, writing tests, reviewing changes, or preparing commits. The
project is an experimental kernel, so small naming and boundary mistakes become
expensive later. Prefer precise ownership and explicit semantics over quick
local convenience.

Do not use `.clang-format` to rewrite whole files unless explicitly asked.
Prefer small, manual patches that preserve surrounding style.

## Operating Rules

- Read the surrounding code before changing it. Match the local owner,
  vocabulary, and failure style.
- Treat the worktree as shared with the user. Never revert or overwrite changes
  you did not make unless the user explicitly asks.
- Use `grep`, `find`, `git diff`, and targeted file reads to verify naming and
  boundary changes. `rg` is preferred when available, but this repository may
  not have it installed.
- Use `apply_patch` for manual edits. Do not use shell redirection or ad hoc
  scripts to rewrite source files unless a mechanical rename is clearer and you
  inspect the result afterward.
- Keep behavior changes, structural cleanup, and pure naming changes separate
  unless the accepted plan explicitly combines them.
- For MM, HAL, boot, or SMP changes, assume the user expects implementation,
  verification, and a concise summary, not just a proposal.

## Source Of Truth

- Hardware facts for x86_64 come from the Intel and AMD manuals under
  `/home/tairitsu/Projects/docs`.
- XNU sources under `/home/tairitsu/Projects/xnu` are a layering and evolution
  reference, not code to copy and not a naming source to import verbatim.
- External protocol headers such as `include/limine.h` and
  `include/multiboot2.h` are imported protocol mirrors. Avoid style churn there
  unless a protocol integration change requires it.
- Public Plane APIs keep the `plane_` or `hal_` prefix. Do not introduce XNU
  aliases such as `vm_map_*`, `vm_object_*`, or `pmap_*` as public generic APIs.

## Code Style

- Use Linux brace style: function definition braces go on the next line;
  `if`, `for`, `while`, `switch`, and `else` braces stay on the same line.
- Use tabs for indentation in C, headers, and assembly-adjacent code.
- Keep manual line wrapping intentional. Do not run broad auto-formatting that
  changes hand-tuned widths.
- Use lowercase hex prefixes and suffixes in project code, such as `0x1000ull`.
- Do not place `(void)` before function calls. Keep `(void)arg` only for
  intentionally unused parameters or compile-time expressions.
- Prefer named local variables over dense expressions when the value is used in
  diagnostics, rollback, or hardware bit construction.
- Add and use compiler attribute wrappers in `plane/compiler.h`, such as
  `__noreturn`, `__packed`, `__used`, `__weak`, `__aligned(x)`, and
  `__section(name)`, instead of spelling raw `__attribute__((...))` in project
  code.
- Keep file-local helpers `static`. Static helpers usually should not carry the
  full public owner prefix; the file already supplies that ownership context.
  Use a short local name such as `reset_free_queue()` unless a longer prefix is
  needed to distinguish multiple local subdomains.
- Cross-file symbols must belong to a clear owner cluster and have that
  cluster's prefix.

## Comments

- Prefer short `/* ... */` comments.
- Add comments at module boundaries, public APIs, non-obvious assumptions,
  manual/spec references, and TODO boundaries.
- Do not narrate obvious code. Comments should explain why a boundary exists or
  what assumption the code relies on.
- Do not leave historical stage names such as "v1", "v2", or "v3" in comments
  unless they describe a real external version. Use semantic wording such as
  "Plane-owned physmap", "bootstrap mapping", or "kernel dynamic map window".
- When a workaround is intentionally temporary, say what owns the final fix.
  Example: an IO or framebuffer bootstrap bridge must name the later IO-map or
  cache-attribute path that should replace it.
- Keep hardware comments anchored to the manual chapter or field semantics, but
  do not paste long manual excerpts.

## Naming Vocabulary

Use these words consistently:

- `boot`: boot protocol, bootloader handoff, or protocol parser code.
  Examples: `boot_limine_*`, `boot_mb2_*`, `struct plane_boot_info`.
- `bootstrap`: temporary startup bridge or bootstrap storage.
  Examples: MB2 bootstrap framebuffer map, physmap bootstrap window, VM metadata
  bootstrap storage.
- `startup`: kernel startup phase or AP startup state/entry.
  Examples: `plane_smp_startup_*`, `plane_cpu_startup_state`.
- `framebuffer`: the early display resource handed off by the bootloader and
  later remapped through IO-map. Do not call this "video" or "boot video".
- `memmap`: Plane memory-map ownership and sanitization. Prefer
  `plane_memmap_*`.
- `physmap`: Plane's RAM physical map. Do not use `direct_map` in new names.
- `pmap`: page-table construction, active kernel mappings, root ownership, and
  TLB invalidation.
- `local_interrupt`: generic HAL name for a local interrupt controller. Do not
  expose x86-specific LAPIC terms through generic HAL names.

Preferred owner/verb/object order:

- For operation-style C symbols, prefer `plane_<owner>_<verb>_<object>()`.
  Keep the verb immediately after the owner cluster so related actions line up,
  such as `plane_vm_page_reset_runtime()` and
  `plane_vm_page_reset_resident_links()`.
- Apply the same rule to arch-private helpers:
  `x86_64_physmap_set_bootstrap_window()`,
  `x86_64_physmap_install_bootstrap_window()`, and
  `x86_64_pmap_build_physmap_in_owned_root()`.
- Query/property helpers may use noun-like names when they read naturally, such
  as `plane_cpu_current_id()`, `plane_vm_page_wire_count()`, or
  `x86_64_pmap_current_root_phys()`.
- Getter-style APIs are tolerated when they already exist, but do not mix
  `owner_object_get()` and `owner_get_object()` inside one owner cluster. Rename
  them only in a dedicated cleanup.
- Existing `plane_pmm_get_stats()` and `plane_vm_map_get_stats()` are older
  public API names. Do not churn them opportunistically in unrelated patches.
- Do not force static file-local helpers to carry public owner prefixes just to
  match exported symbols. Prefer concise local names when the containing file
  already makes the owner obvious.

Avoid these stale names in new code:

- `boot_video`, `plane_video_info`, `plane_boot_video_*`
- `boot_mem`, `plane_sanitize_memory_map`
- `smp_boot`, `plane_smp_boot_*`, `PLANE_CPU_BOOT_*`, `boot_state`
- `arch_mmu`, `mb2_early_mmu`, `pmap_active.c`, `msr_internal.h`
- `direct_map` for the runtime physmap path

## Owner Clusters

Plane uses an XNU-like owner cluster rule. `*_internal.h` means "private to the
owning cluster", not necessarily "only included by one .c file".

- Generic `include/plane/` is public Plane kernel API.
- Generic `include/hal/` is public HAL API. It must not expose x86_64, GRUB,
  Limine, or Multiboot2 implementation details.
- `hal/x86_64/` is the x86_64 owner cluster. Its internal headers may be shared
  by x86_64 implementation files and white-box tests.
- `boot/limine/` and `boot/multiboot2/` are boot protocol parser clusters. They
  parse protocol data and fill Plane handoff structures.
- `hal/x86_64/boot/*` is the x86_64 boot adapter cluster. It may include x86_64
  internal headers because it translates boot handoff into x86_64 state.
- `kernel/mm/` is the MM owner cluster. It may share VM object/page/map/zone
  internal headers, but public callers should use `include/plane/*`.
- `kernel/smp.c` owns CPU topology/runtime state. `kernel/smp_startup.c` owns
  AP stack preparation and AP startup launch helpers.

Do not create a new facade header or one-line source file just to avoid an
internal include when the caller is in the same owner cluster. Conversely, do
not let a boot parser include arch-private MM/pmap/physmap internals directly.

## Header Rules

- Include the most specific header needed by the file.
- Do not add the repository root as a global include root.
- `hal/` is the arch-private include root. x86_64 internals should be included
  as `<x86_64/foo_internal.h>` or `<x86_64/foo.h>` when the header is an
  arch-private service.
- Public x86_64 hardware definition headers live under `include/hal/x86_64/`
  only when assembly or other public arch code legitimately needs them.
- `plane/util.h` is for generic utility macros such as `container_of`,
  `ARRAY_SIZE`, and alignment helpers. It must not include `plane/bits.h`.
- Files that need bit operations must explicitly include `plane/bits.h`.
- Keep C-only helpers behind `#ifndef __ASSEMBLER__` in headers shared with
  assembly.
- Use named macros for values with architecture, protocol, ABI, or page-table
  meaning. Do not macro-ize ordinary local test values or obvious loop counts
  just to remove every literal.

## x86_64 Hardware Definitions

- Keep hardware register and bit definitions in focused definition headers:
  - `address_space.h`: x86_64 virtual layout and physmap geometry.
  - `paging_defs.h`: page-table entry bits, masks, indices, and helpers.
  - `descriptor_defs.h`: GDT/TSS selectors, descriptor encodings, TSS layout.
  - `interrupt_defs.h`: IDT gates, exception vectors, interrupt frame, #PF
    error-code bits.
  - `processor_defs.h`: RFLAGS, CR0/CR4, EFER, PAT entry encodings used by
    startup paths.
  - `msr_defs.h`: MSR numbers and MSR field bits.
  - `cpuid_defs.h`: CPUID leaves, registers, and field masks used by decoders.
  - `lapic_regs.h`: LAPIC register-space offsets and field helpers.
- Implementation files should express behavior: probe, configure, map, clone,
  enter, teardown, dispatch. They should not grow large piles of magic hardware
  constants.
- Do not add unused hardware constants just because they exist in the manual.
  Add fields when code or tests use them.
- If a generic HAL API would expose x86-only terms such as LAPIC, fixed IPI,
  xAPIC, x2APIC, PAT, or PTE, keep that detail in x86_64 internals and map it
  to a generic concept.

## Boot, Handoff, And Startup

- Boot protocol parsers collect handoff data. They should not own runtime MM,
  pmap, physmap, SMP, or device mapping policy.
- Limine:
  - `boot_limine_arch_handoff` carries Limine data needed by the x86_64 arch
    adapter.
  - `boot_limine_arch_install_hhdm_physmap()` installs the Limine HHDM as the
    bootstrap physmap window.
  - `boot_limine_arch_hhdm_virt_to_phys()` converts Limine HHDM framebuffer VAs
    to physical addresses for handoff.
- Multiboot2:
  - MB2 bootstrap mapping code is a pre-kmain bridge only.
  - Use `mb2_bootstrap_map` naming for MB2 bootstrap framebuffer and identity
    mapping helpers.
  - Do not present MB2 bootstrap mapping helpers as generic MMU APIs.
- `struct plane_boot_info` is the generic handoff into `kmain()`.
- `struct plane_framebuffer_info` describes the current framebuffer state,
  including bootloader handoff fields and the runtime IO-map VA after remap.
- `kmain()` may remain the orchestration point for now. Do not split it unless
  explicitly planned.

## Diagnostics

- Use `BUG()`, `BUG_ON()`, and `BUG_ON_MSG()` for unrecoverable kernel contract
  failures, such as corrupted boot handoff data, missing required CPU
  capabilities, impossible init failures, or conditions where continuing would
  hide root cause.
- Use `WARN_ON*()` only when the kernel can continue running and the condition
  is still worth recording.
- `BUG_ON*()` and `WARN_ON*()` conditions are evaluated, following Linux
  kernel-style semantics rather than C `assert()`/`NDEBUG` semantics.
- Simple must-succeed boolean calls are acceptable in `BUG_ON_MSG(!fn(...))`.
- Split complex rollback, multi-step actions, multiple results, or unclear
  `WARN_ON*()` side effects into named local variables.
- Library-style helpers should usually return `false` or an error sentinel and
  let the caller decide whether to `BUG_ON_MSG()`.
- Do not use silent `hal_cpu_hang()` from C paths when printk/serial
  diagnostics are available. Keep raw hangs for very early assembly or terminal
  halt paths where diagnostics cannot be trusted.

## Strong Types

- Use `plane_vaddr_t` for virtual addresses crossing subsystem boundaries.
- Use `plane_paddr_t` for physical addresses crossing subsystem boundaries.
- Do not use raw `uint64_t` for a public physical or virtual address unless the
  value is still in an external protocol struct or page-table bit packing.
- Extract raw addresses only at the point where arithmetic, encoding, or
  hardware access requires it.
- Prefer helpers such as `plane_vaddr_make()`, `plane_vaddr_raw()`,
  `plane_vaddr_is_null()`, `plane_vaddr_is_page_aligned()`,
  `plane_vaddr_add_pages()`, and the corresponding `plane_paddr_*` helpers.
- Keep protocol raw fields near the parser. Arch adapters should receive strong
  address types whenever possible.

## MM, PMM, VM, And pmap

- PMM is still an early allocator. Only documented PMM allocator/free queue
  locking exists; do not assume broader MM SMP safety or atomic semantics until
  those are added explicitly.
- Use existing `plane_spinlock` plus irqsave for early MM metadata locks.
  Do not introduce sleep locks or wait-based synchronization before scheduler
  and wait queues exist.
- Add locks at owner-cluster boundaries and document what each lock protects.
  Prefer a small explicit lock order over broad lock nesting. Current VM object
  internal order is object lock before resident hash lock. Current cross-owner
  MM order is VM map lock before VM object lock.
- PMM has its own irqsave spinlock for allocator globals, metadata placement,
  free queue use, and allocator lifecycle transitions in `struct plane_page`.
  Do not allocate/free PMM pages while holding VM map or VM object locks.
- Physical page 0 is reserved as a null physical guard. PMM must never allocate
  or manage it.
- `struct plane_page` is the XNU-like page metadata foundation. Do not expand
  it into pageout, coloring, per-CPU queues, or pager state without a separate
  plan.
- Physical allocation APIs return physical addresses, not dereferenceable
  virtual addresses. Use physmap conversion helpers to access page contents.
- `plane_vm_object_allocate()` returns a refcounted internal object from the VM
  metadata zone. Caller-owned initialized objects are not zone-owned.
- `plane_vm_map_enter()` owns its map-entry object reference. Delete, free,
  overwrite, and zap-list disposal must drop entry-held object references.
- `plane_vm_map_wire_pages()` and `plane_vm_map_unwire_pages()` are map metadata
  operations only. Fault-layer wiring is responsible for resident page and PMM
  wire-count synchronization.
- `plane_vm_fault_page()` is split conceptually into lookup/precheck,
  page-resolve/zero-fill, and pmap-enter/repair. Preserve rollback rules for
  zero-fill pages and resident-hit failures.
- `plane_vm_fault_pages()` is a side-effectful range prefault wrapper. It is not
  all-or-nothing.
- `plane_vm_fault_wire_pages()` rolls back wiring added by the failed operation,
  but it may keep successfully faulted resident pages.

## Physmap, IO-map, And pmap

- `physmap` is Plane's RAM-only physical map. It is not the formal path for
  device memory.
- Bootloader HHDM or MB2 bootstrap mappings are temporary bootstrap physmap
  windows. After page-table ownership, Plane installs its owned physmap.
- `hal_mmu_enable_physmap()` is the generic startup-stage entry for enabling
  RAM physmap conversion. Arch-private state and window geometry stay in
  x86_64 physmap internals.
- `hal_mmu_physmap_phys_to_virt()`,
  `hal_mmu_physmap_phys_range_to_virt()`, and
  `hal_mmu_physmap_virt_to_phys()` must enforce actual required coverage, not
  blindly expose the whole rounded window.
- `plane_io_map()` is the formal runtime path for device and framebuffer
  mappings. IO-map entries are VA reservations plus pmap mappings; they do not
  create anonymous VM objects and faults into them must fail.
- pmap owns active root access, page-table mutation, pmap root cloning,
  Plane-owned physmap construction, and local TLB invalidation.
- Mutating x86_64 pmap root helpers operate only on PMM-owned page-table roots.
  Read-only translation helpers may inspect any physmap-accessible root.
- Active kernel pmap wrappers are responsible for local TLB invalidation.
  Root-parameter helpers must not invalidate TLB entries.
- Mapping attributes are explicit pmap semantics. Prefer
  `struct hal_mmu_map_options { prot, attr }` over ad hoc bitsets.
- `hal_mmu_protect_kernel_page()` changes current protection only and must
  preserve existing cache attribute bits.

## SMP

- Current SMP support is a foundation only. APs may be discovered, prepared,
  started, and parked, but they must not enter general kernel execution.
- MM, pmap, VM resident metadata, and kmem are still BSP-only unless a patch
  explicitly introduces locking and verifies it. PMM has only the documented
  allocator/free queue lock boundary.
- `plane_cpu_data` owns per-CPU state; `self` must point to its own object.
- BSP startup installs current CPU data through the arch hook before exposing
  initialized SMP state.
- AP park sequence should remain: disable IRQ, install descriptor/TSS context,
  install current CPU data, initialize local interrupts, mark parked, halt.
- `plane_smp_startup_*` names are for AP startup launch helpers. Do not revive
  `plane_smp_boot_*`.
- Local APIC/xAPIC details stay in x86_64 local interrupt implementation.
  Generic HAL callers use `hal_local_interrupt_*`.

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
- Tests may include arch-private or MM internal headers when they explicitly
  test those owner clusters.
- Keep test names and assertion labels in the current vocabulary. Avoid stale
  labels such as "old boot", "early MMU", "direct map", or "bootstrap object"
  when the tested object is now zone-backed or physmap-owned.

## Build, Configuration, And Generated Files

- Do not commit `.config`, `include/generated/autoconf.h`, `build/`, `*.o`,
  `*.d`, `*.elf`, `*.iso`, `tools/limine_bin/limine`, or `.vscode/`.
- Do not manually delete `include/generated/`; Makefile rules are responsible
  for recreating and cleaning generated configuration files.
- Use `make clean` or `make distclean` for build cleanup, and confirm
  `include/generated/` still exists afterward.
- The user's normal local build uses the conda environment named `plane` and
  the cross toolchain under `/home/tairitsu/opt/cross/bin`.
- Limine local config:
  - `.config` contains `CONFIG_X86_64=y` and `CONFIG_BOOT_LIMINE=y`.
  - `include/generated/autoconf.h` contains the corresponding generated macros.
- MB2/GRUB smoke config is temporary:
  - `.config` contains `CONFIG_X86_64=y` and `CONFIG_BOOT_GRUB=y`.
  - Always restore Limine config afterward unless the user asks otherwise.

## Verification

Run verification proportional to the touched code. For this repository, boot
paths are part of MM/HAL correctness.

- Always run `make unit-check` for code changes unless the user explicitly asks
  for a non-validated draft.
- Always run `git diff --check`.
- For MMU, pmap, physmap, IO-map, framebuffer, boot, HAL, SMP startup, or
  kernel initialization changes, also verify Limine and MB2/GRUB QEMU smoke.
- Smoke means booting long enough to see:
  `Kernel initialization completed. System halted.`
- QEMU timeout exit code `124` is acceptable when the serial output reached the
  expected halt message.

Common verification commands:

```sh
make unit-check
git diff --check
```

Limine:

```sh
source /home/tairitsu/miniconda3/etc/profile.d/conda.sh
conda activate plane
PATH=/home/tairitsu/opt/cross/bin:$PATH make all iso
timeout 12s qemu-system-x86_64 -M q35 -m 2G -cdrom plane.iso -boot d -serial stdio -display none -no-reboot
timeout 12s qemu-system-x86_64 -M q35 -m 4G -cdrom plane.iso -boot d -serial stdio -display none -no-reboot
```

MB2/GRUB:

Temporarily edit `.config` to:

```text
CONFIG_X86_64=y
CONFIG_BOOT_GRUB=y
```

Then run:

```sh
source /home/tairitsu/miniconda3/etc/profile.d/conda.sh
conda activate plane
genconfig --header-path include/generated/autoconf.h
PATH=/home/tairitsu/opt/cross/bin:$PATH make all iso
timeout 12s qemu-system-x86_64 -M q35 -m 2G -cdrom plane.iso -boot d -serial stdio -display none -no-reboot
timeout 12s qemu-system-x86_64 -M q35 -m 4G -cdrom plane.iso -boot d -serial stdio -display none -no-reboot
```

Restore Limine:

Edit `.config` back to:

```text
CONFIG_X86_64=y
CONFIG_BOOT_LIMINE=y
```

```sh
source /home/tairitsu/miniconda3/etc/profile.d/conda.sh
conda activate plane
genconfig --header-path include/generated/autoconf.h
PATH=/home/tairitsu/opt/cross/bin:$PATH make all
```

## Commit Hygiene

- Stage only the files relevant to the change. Avoid `git add .`.
- Keep commits focused: style cleanup, architecture boundary changes, and
  behavior changes should be separate unless a plan explicitly combines them.
- Do not stage generated or local build artifacts.
- `tools/limine_bin/limine` is a generated build artifact and must remain
  untracked.
- Before committing code changes, report the tests and boot smokes that passed,
  plus any validation that could not be run.
