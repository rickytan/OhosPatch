# OhosPatch Agent Context

## Project

- Repository: `git@github.com:rickytan/OhosPatch.git`
- Original iOS project: <https://github.com/rickytan/FIXiT>
- Main branch: `main`
- Purpose: transparent runtime JavaScript patching for HarmonyOS/OpenHarmony, inspired by FIXiT.
- Workspace project root: this directory.
- The reusable patch implementation is the `ohospatch` HAR module.
- `entry` is only a Demo APP that consumes the HAR and demonstrates behavior.
- `patch-server` serves a patch dynamically. Patch code must not be bundled into the HAR or HAP.

## Product Requirements Established With The User

- The host APP must not modify business classes to opt in to patch dispatch.
- Business classes must not inherit a base class, add decorators, or call a patch router.
- Patch scripts are downloaded dynamically by the host. The HAR does not download or verify signatures.
- The host passes either a complete patch string or a local absolute file path to `OhosPatch`.
- The host owns download policy, signature verification, version matching, rollout, caching, rollback, timeout, and circuit breaking.
- The patch runtime uses a new JSVM, analogous to JavaScriptCore in iOS FIXiT.
- JSVM and the ArkTS main VM do not share objects or prototype chains.
- Native code resolves ArkTS modules with `napi_load_module_with_info` and patches methods in the ArkTS VM.
- Instance methods are replaced on `constructor.prototype`; static methods are replaced on the constructor.
- C++ must never throw or catch exceptions. It is compiled with `-fno-exceptions`.
- C++/JSVM/NAPI errors must be logged with error-level HiLog and must not terminate the process.
- ArkTS and patch JavaScript may throw.
- The original ArkTS exception must remain pending and must not be converted or swallowed by C++.

## Current Runtime API

The embedded runtime is `ohospatch/src/main/cpp/runtime/fixit.js`.

- `fixit.d.js` is the editor-only JSDoc declaration for every public Patch context API. Keep its `@version`, signatures, constraints, and globals synchronized with the embedded runtime.
- `skills/ohospatch/` is the portable Codex/Claude authoring Skill source. Keep it and `scripts/install-skill.sh` aligned with `fixit.d.js`, README limitations, and demonstrated Runtime behavior.

- `Fixit.fix(fullPath)` and `Fixit.component(fullPath)` parse complete OHM paths internally; do not use `require()` to create target descriptors.
- `Fixit.import(fullPath)` synchronously loads an exported ArkTS class and returns a persistent host-VM Proxy supporting static calls, `new`, instance calls, properties, arguments, and Patch results. References are released by `clear()` or Patch replacement.
- `Fixit.registerTarget(className, descriptor)`
- `instanceMethod(name, handler)` / `classMethod(name, handler)`
- `require(fullPath)` is a compatibility alias of `Fixit.import(fullPath)` and returns a callable ArkTS class Proxy.
- `nil`, `Nil`, `isNil`, `nilToNull`, `nullToNil`
- `console.debug/log/info/warn/error` bridged to HiLog.
- `setTimeout/clearTimeout`
- `setInterval/clearInterval`
- `setImmediate/clearImmediate`
- `queueMicrotask`

Timer callbacks and arguments remain in JSVM. Native stores only timer IDs and libuv handles. Timers use the host N-API libuv event loop and are cancelled by `clear()`, patch replacement, or VM reset. Native currently allows 256 active timers.

## Native Architecture

Primary implementation: `ohospatch/src/main/cpp/ohospatch.cpp`.

- One process-wide `JsvmRuntime` owns the independent JSVM.
- Embedded `fixit.js` is generated into `fixit_runtime.h` by CMake.
- Patch method registrations are returned from JSVM as JSON specs.
- Native loads target classes in the ArkTS VM and installs N-API trampolines.
- Handler arguments still enter JSVM as JSON values. Method handler `this`, Component event handler `this`, nested properties, method calls, Proxy arguments, original-method results, and Proxy returns use invocation-scoped Native handles.
- Runtime `1.4.0` creates a JS `Proxy` for the ArkTS receiver. `get`, `set`, and `apply` synchronously bridge to the original ArkTS object, preserving nested object identity and prototype method dispatch.
- Proxy handles are valid only during the current synchronous patch invocation and must not escape to timers, promises, or globals. A call can retain at most 256 handles.
- Hook failures fall back to the original ArkTS method.
- Installation failure restores hooks that were already installed.
- `clear()` restores original methods and clears the JS registry and timers.
- Fixed-size arrays are preferred over exception-throwing containers in native control paths.
- Allocation must use `std::nothrow`; always handle allocation failure.
- Do not add `throw`, `catch`, `napi_throw`, or `OH_JSVM_Throw` in C++.

Native libraries linked by the HAR:

- `libace_napi.z.so`
- `libhilog_ndk.z.so`
- `libjsvm.so`
- `libuv.so`

## Module Structure

```text
OhosPatch/
|- ohospatch/       reusable HAR and all patch implementation
|- entry/           Demo APP only
|- patch-server/    dynamic HTTP patch service
|- .github/         GitHub Actions workflow
`- README.md
```

Do not move download, signature, URL, cache, or startup policy into `ohospatch`.

## Declarative Component DSL Direction

The user requested a DSL for repairing declarative ArkUI components, including component parameters/state, node attributes, and callbacks such as `onClick`.

API 20 generated output was inspected at:

`entry/build/default/cache/default/default@CompileArkTS/esmodule/debug/entry/src/main/ets/pages/Index.ts`

Important generated shape for state-management V1:

- Source `build()` becomes generated `initialRender()`.
- Component parameters flow through `setInitiallyProvidedValue(params)` and `updateStateVars(params)`.
- Each ArkUI node is created through `this.observeComponentCreation2(builderCallback, ComponentApi)`.
- The builder callback calls methods such as `Button.createWithLabel`, `Button.height`, and `Button.onClick`.
- `rerender()` calls `updateDirtyElements()`.
- An `@Entry` page can be non-exported and registered only through `registerNamedRoute`.

The public DSL must not expose generated names such as `initialRender` or `observeComponentCreation2`. An API/version adapter owns those details.

Proposed public concepts:

- `Fixit.component(fullPath)` parses the complete OHM target path internally.
- `component.param(name)` for incoming component parameters.
- `component.state(name)` for observable component state.
- `component.node(selector)` for a built-in ArkUI node.
- `node.attrs({...})` for attribute overrides.
- `node.event(name, rule)` for callback replacement.
- Event modes eventually include `replace`, `before`, `after`, and `around`.
- Event context eventually provides safe state snapshots, `setState`, component method invocation, and original callback invocation.

### Implemented status (2026-08-10)

- Runtime version `1.4.0` implements `Fixit.component` and the invocation-scoped ArkTS object Proxy bridge.
- API 20 state-management V1 exported custom components are supported.
- `param().transform/replace` and `state().transform/replace` are implemented.
- Node selection by `{ type, occurrence }` is implemented with zero-based per-type counting.
- `attr`, `attrs`, and synchronous `event(..., { mode: 'replace' })` are implemented.
- Event `capture` and `context.setState()` are implemented; capture is limited to 16 properties.
- Component event handlers written with normal `function` syntax receive `this` as the current Component instance Proxy. Arrow functions keep lexical `this`.
- Event bridges retain the original ArkTS callback and fall back to it after `clear()` or a patch handler failure.
- The Demo target is `entry/src/main/ets/demo/PatchablePanel.ets` and the dynamic script is `patch-server/patch.js`.
- API 20 generated output was inspected and matches `setInitiallyProvidedValue`, `updateStateVars`, `initialRender`, and `observeComponentCreation2`.
- The HAP was installed on a HarmonyOS 6.0.2 API 22 Pura 90 emulator after compiling against API 20.
- Device screenshots verified patched parameter text, state `40`, Button attributes, and click replacement from `40` to `50`.
- HiLog verified 8 installed rules/hooks and the process remained alive after prior JSVM crash fixes.

Implementation should proceed in explicit capability phases:

1. API 20, state-management V1, exported custom components.
2. Component parameter/state transforms.
3. Node selector by component type plus occurrence.
4. Basic scalar node attributes.
5. Synchronous event replacement, initially `onClick` and then generic event adapters.
6. Route factory interception for non-exported `@Entry` pages.
7. ID/create-argument/hierarchy selectors, resources, live-instance refresh, original event callbacks, and V2 adapters.

Fail closed when a component shape, node selector, attribute, or event cannot be verified. Log an error and leave business behavior untouched.

## Component DSL Technical Cautions

- Patching `build()` is not viable because that method is absent after compilation.
- Exported custom components can be resolved through the module namespace. Non-exported route pages need a route factory adapter.
- A target-specific wrapper around `observeComponentCreation2` can wrap each node builder callback.
- Attribute overrides should run after the original builder callback so the patch is the last writer.
- Event replacement can register a new callback after the original event registration.
- Calling the original event later requires capturing the original ArkTS callback while the builder runs; do not claim `around/origin` support before this is implemented and tested.
- Stored callbacks and component instances need N-API reference lifecycle management. Prefer weak component references.
- Existing mounted components need rerender/invalidation after install and clear. If that cannot be guaranteed, report that a natural rerender is required.
- Click/touch event objects may contain native state and cannot be passed through the current generic JSON bridge. Define safe event DTOs.
- Resource, Length, Color, enum, animation, gesture, and controller values need typed wire descriptors rather than arbitrary JSON.
- A JS event context must not outlive the synchronous callback until weak-reference async semantics are implemented.
- Global ArkUI wrappers must be pass-through outside an active target render frame.
- V1 and V2 generated code differ. Keep version adapters separate.
- Every native-to-JSVM operation that creates or retrieves a JSVM value must run inside an explicit `JSVM_HandleScope`.
- `JSVM_CallbackStruct` passed to `OH_JSVM_CreateFunction` must have stable process/VM lifetime; never allocate it on a temporary stack frame.

## Build And Test

Run JS/runtime tests:

```bash
npm test
```

Build HAR:

```bash
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
  --mode module -p module=ohospatch clean assembleHar --no-daemon
```

Build Demo HAP:

```bash
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
  --mode module -p module=entry clean assembleHap --no-daemon
```

Relevant local tool paths:

- SDK root: `$HOME/Library/OpenHarmony/Sdk`
- API 20 native tools: `$HOME/Library/OpenHarmony/Sdk/20/native/llvm/bin`
- DevEco Studio: `/Applications/DevEco-Studio.app/Contents`
- HDC: `$HOME/Library/OpenHarmony/Sdk/20/toolchains/hdc`

Native verification should include checking the unstripped `libohospatch.so` for exception symbols:

```bash
llvm-nm -D -u libohospatch.so | \
  rg '__cxa_(throw|begin_catch|rethrow)|_Unwind_RaiseException'
```

Expected result: no matches.

## Tests And CI

- JS tests: `ohospatch/src/test/js/fixit.test.mjs`
- Native safety tests: `ohospatch/src/test/js/native-safety.test.mjs`
- GitHub workflow: `.github/workflows/harmonyos-build.yml`
- Push and PR run JS tests on GitHub-hosted Ubuntu.
- HAR/HAP packaging uses a self-hosted macOS ARM64 runner labeled `harmonyos`.
- `HARMONYOS_CI_ENABLED=true` enables package builds on main pushes.
- Packaging uploads unsigned HAR/HAP artifacts.

## Demo

- Host startup task downloads `http://127.0.0.1:8080/patch.js`.
- Start server with `node patch-server/server.mjs`.
- Use `hdc rport tcp:8080 tcp:8080` for simulator/device access.
- The current remote patch demonstrates prototype hooks and timer execution.
- No patch script is bundled into the application package.

## Working Rules

- Read the generated ArkTS output after any component experiment; source syntax alone is insufficient.
- Keep edits scoped. Do not undo unrelated user changes.
- Use `apply_patch` for manual file edits.
- Run `git diff --check`, tests, HAR build, and HAP build before committing.
- Inspect the final binary for C++ exception symbols.
- If a simulator is unavailable or locked, state that device validation was not completed.
- Commit and push finished work to `origin/main` unless the user asks otherwise.
- Never commit build outputs, `.hvigor`, `oh_modules`, `node_modules`, `local.properties`, HAP, HAR, or signing material.
