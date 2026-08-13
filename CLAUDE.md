# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` is the canonical, detailed agent context for this repo — read it for product requirements, runtime API, native architecture, and component-DSL direction. This file captures the operational essentials and the cross-file invariants that are easy to break.

## What this is

OhosPatch is a transparent runtime JavaScript patching system for HarmonyOS/OpenHarmony, porting the iOS [FIXiT](https://github.com/rickytan/FIXiT) approach. The host APP downloads and verifies a patch, then hands OhosPatch a complete JS string or a local absolute file path. OhosPatch runs the script in an independent JSVM and replaces business methods on ArkTS prototypes. Business classes opt in to **nothing** — no base class, no decorator, no router call.

## Commands

Runtime behavior tests run on a connected HarmonyOS/OpenHarmony emulator or device. They build and install the Demo HAP plus `entry@ohosTest`, then execute Hypium against the real JSVM/N-API/ArkTS environment:

```bash
npm test
```

Build the reusable HAR (output: `ohospatch/build/default/outputs/default/ohospatch.har`):

```bash
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
  --mode module -p module=ohospatch clean assembleHar --no-daemon
```

Build the Demo HAP:

```bash
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
  --mode module -p module=entry clean assembleHap --no-daemon
```

Verify the native binary has no C++ exception symbols (must be empty):

```bash
$HOME/Library/OpenHarmony/Sdk/20/native/llvm/bin/llvm-nm -D -u \
  ohospatch/build/default/intermediates/cmake/default/obj/arm64-v8a/libohospatch.so \
  | rg '__cxa_(throw|begin_catch|rethrow)|_Unwind_RaiseException'
```

Run the demo patch server + device access:

```bash
node patch-server/server.mjs          # serves http://127.0.0.1:8080/patch.js
hdc rport tcp:8080 tcp:8080           # reverse port to device/simulator
```

Do not add Node `vm` Patch runtime tests or source-scanning runtime tests for `ohospatch`; validate runtime behavior through device-side inputs and outputs. Before committing: `git diff --check`, `npm test`, and (when native changed) HAR + HAP build + exception-symbol check.

## Architecture: two cooperating VMs

This is the central concept — a single change usually spans all three layers below.

```
Host ArkTS VM  ──executeScript/executeFile──▶  Native (libohospatch.so)
   ▲                                                 │ creates independent JSVM
   │ napi_load_module_with_info                      ▼
   │ loads target class                          JSVM runs fixit.js (built-in)
   │                                                 then the host patch
   │ ◀──patch returns JSON hook specs────────────────┘
   │
prototype[methodName] = native trampoline ──▶ calls into JSVM handler
```

1. `OhosPatch.ets` (ArkTS facade) forwards to `libohospatch.so` native `executeScript`/`clear`.
2. Native `JsvmRuntime` (one process-wide instance, in `ohospatch.cpp`) creates an independent JSVM via `OH_JSVM_CreateVM`. **JSVM and the ArkTS main VM do not share object heap or prototype chain** — editing `DemoViewModel.prototype` inside JSVM alone does nothing to ArkTS objects.
3. The built-in `fixit.js` runtime is embedded into the `.so` at build time (CMake reads `runtime/fixit.js` → generates `fixit_runtime.h` via `fixit_runtime.h.in`). It runs first, then the host patch. Host and patch never load it themselves.
4. A patch calls `Fixit.fix(fullPath)` / `Fixit.component(fullPath)`, which parse the OHM path internally and return JSON hook specs to Native.
5. Native loads the target ArkTS module with `napi_load_module_with_info` in the **main** VM, then replaces instance methods on `constructor.prototype[methodName]` and static methods on `constructor[methodName]`. The original function is held as a `napi_ref`; the replacement is a Native trampoline back into JSVM.
6. On each call, Native builds an invocation-scoped ArkTS handle table. The JSVM handler's `this` is a `Proxy`; `get`/`set`/`apply` synchronously bridge to the original ArkTS instance, preserving nested object identity and prototype dispatch. `origin.apply(this, arguments)` calls the saved original.
7. `clear()` restores original prototypes and releases hook/component/import refs and timers.

**Proxy lifetime**: invocation-scoped handles are valid only during the current synchronous handler or `origin` call. They must not escape to timers, promises, or globals. `Fixit.import()` returns a *persistent* Proxy (valid until `clear()`/replacement) — distinct from the per-call Proxy.

### Where each layer lives

- `ohospatch/` — the **entire** patch implementation, built as a reusable HAR. `Index.ets` → `src/main/ets/OhosPatch.ets` (ArkTS facade) → `src/main/cpp/ohospatch.cpp` (3k-line native core) + `src/main/cpp/runtime/fixit.js` (embedded JS runtime).
- `entry/` — Demo APP only. Consumes the HAR via `file:../ohospatch`. Contains **no** native or patch implementation source. Business demo classes (`DemoViewModel`, `PatchablePanel`, `Point`) do not reference OhosPatch.
- `patch-server/` - dev-only HTTP server (`server.mjs`); serves `entry/src/main/resources/rawfile/patch.js` directly. The server is not packaged; the rawfile patch is the HAP fallback.
- `skills/ohospatch/references/fixit.d.js` - editor-only JSDoc declaration; not delivered to device, not executed. Installed alongside the skill.

Do **not** move download, signature, URL, cache, or startup policy into `ohospatch` — those are host responsibilities.

## Native C++ invariants (hard rules)

`ohospatch.cpp` is built with `-fno-exceptions`. Runtime safety is verified through device-side behavior tests, and the compiled native binary is checked directly for C++ exception symbols:

- Never add `throw`, `catch`, `std::exception`/`std::runtime_error`, `napi_throw`, or `OH_JSVM_Throw`. Use `std::nothrow` for allocation and always handle failure.
- Prefer fixed-size arrays over exception-throwing containers in native control paths.
- Every JSVM value creation/retrieval must run inside an explicit `JSVM_OpenHandleScope`/`OH_JSVM_OpenHandleScope`.
- `JSVM_CallbackStruct` passed to `OH_JSVM_CreateFunction` needs **stable process/VM lifetime** — declare them `static`, never on a temporary stack frame.
- Timers use the host N-API libuv event loop (`napi_get_uv_event_loop`); callback + args stay in JSVM, Native holds only timer IDs. Max 256 active timers; `clear()`/replacement/VM reset cancels them.
- Failure behavior: JSVM/NAPI bridge errors → error-level HiLog (tag `OhosPatch`), never crash. Patch execution failure → fall back to original ArkTS method. Hook install failure → roll back already-installed hooks. `executeScript` returns `0` on failure.
- Plain fallback calls preserve original ArkTS pending exceptions. Explicit Patch `origin.apply(...)` calls convert a pending ArkTS exception into a catchable JSVM `Error`; if uncaught, OhosPatch falls back to the original ArkTS behavior.

## Cross-file sync invariants

Several files must stay mutually consistent; a change in one usually requires the others:

- `skills/ohospatch/references/fixit.d.js` `@version` **must equal** `Fixit.runtimeVersion` in `ohospatch/src/main/cpp/runtime/fixit.js` (currently `1.16.0`). Signatures, constraints, and globals in the declaration must match the runtime.
- `skills/ohospatch/SKILL.md` + `scripts/install-skill.sh` must track `references/fixit.d.js`, README limitations, and demonstrated runtime behavior.
- `README.md` "当前边界" (current limitations) must reflect actual runtime capability.
- Do not add source-scanning tests that pin implementation tokens such as handle-scope names or callback storage identifiers. When safety policy matters, test user-visible failure behavior on device and inspect the compiled binary.

## Declarative Component DSL

Targets API 20 state-management V1 and V2 **exported** custom components through separate native adapters. `build()` becomes generated `initialRender()` and nodes use `observeComponentCreation2`; V1 params use `setInitiallyProvidedValue`/`updateStateVars`, while V2 params use `initParam`/`updateParam`/`resetParam` and reuse resets through `resetStateVarsOnReuse`. **The public DSL must not expose generated names**. When changing component behavior beyond an established pattern, inspect the generated ArkTS output (`entry/build/.../cache/.../esmodule/.../*.ts`); source syntax alone is insufficient. Fail closed (log error, leave business behavior untouched) when a shape/selector/attribute/event can't be verified.

Parameter and state rules use `param/state(name, value)` or `param/state(name, function (originValue) { ... })`; the function receives the original value and `this` is the synchronous current-component Proxy. This is the only public value-replacement DSL.

## Working rules

- Keep edits scoped; don't undo unrelated user changes.
- After any component experiment, read the generated ArkTS output.
- If a simulator/device is unavailable or locked, state explicitly that device validation was not completed — don't claim it.
- Never commit build outputs, `.hvigor`, `oh_modules`, `node_modules`, `local.properties`, `*.hap`/`*.har`, or signing material (all gitignored).
- Commit and push finished work to `origin/main` unless asked otherwise.
