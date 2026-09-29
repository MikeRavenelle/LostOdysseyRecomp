# x86-64 vs AArch64: behavior differences

The recompiled PowerPC code was only ever run on x86-64 before the macOS port. This
list records where AArch64 behaves differently and what the project does about it,
so Linux AArch64 (and any other ARM target) starts from known ground.

## Handled

- **Memory ordering.** x86 keeps stores in order (TSO); AArch64 does not, like the
  Xbox 360's PowerPC. XenonRecomp emits `lwsync`, `sync` and `eieio` as comments
  only, which was harmless on x86 and wrong on AArch64: the game's file loader read
  a job from its queue before the job's vtable store was visible and called a null
  or stale virtual function (crashes entering Numara from the disc 4 world map).
  `tools/ppc_codegen.py` now places a C++ fence after each barrier (151 sites):
  `acq_rel` for `lwsync`/`eieio` (no instruction on x86, `dmb ish` on AArch64),
  `seq_cst` for `sync`. `python tools/ppc_codegen.py barriers` applies it to existing
  generated sources.
- **Reservations.** `stwcx.` is a `__sync_bool_compare_and_swap` (full barrier on both
  ISAs); `lwarx` is a plain load, ordered by the following CAS.
- **Host/guest shared state.** The GPU ring write pointer is a `std::atomic`
  (sequentially consistent), so the command processor sees completed packets.
- **Floating-point control.** `ppc_context.h` maps PPC rounding modes and
  flush-to-zero to FPCR on AArch64 (RMode, FZ, FZ16); the mapping is correct (its
  comment lists the wrong mode names).
- **SIMD.** simde maps VMX/SSE to NEON with exact x86 semantics (no `SIMDE_FAST_*`
  options), including float-to-int saturation.
- **Pages and stacks.** 16 KiB pages (guest E-range left unmapped with a fault
  probe); guest-code threads get 1 MiB stacks.

## Different, believed harmless

- **Integer division by zero / INT_MIN ÷ -1.** x86 traps, AArch64 returns 0 /
  INT_MIN, PowerPC leaves the result undefined. The x86 build would crash where
  AArch64 continues; no case seen.
- **Fused multiply-add.** Clang contracts `a * b + c` inside one expression on AArch64
  (`-ffp-contract=on`); the x86 build (Sandy Bridge, no FMA) does not. PowerPC
  `fmadd` is fused, so AArch64 is closer to the original for those instructions.
- **`char` signedness.** Signed on Apple arm64 and x86, unsigned on Linux AArch64.
  Audit host code that relies on `char` being signed before a Linux AArch64 build.

## Still to audit

- Runtime kernel objects shared with guest threads (events, critical sections,
  interlocked helpers in `kernel/`): confirm atomics or locks on every
  cross-thread field.
- Guest code that spins on a plain load without a barrier (would need the load to be
  re-read; the compiler may hoist it). None found yet.
- `vrefp`/`vrsqrtefp` precision via simde versus x86 `rcpps`/`rsqrtps` (12-bit
  estimates on both, different rounding).
