---
name: ohospatch
description: Write, review, and repair OhosPatch JavaScript patch scripts for HarmonyOS/OpenHarmony applications. Use when an AI agent needs to translate an ArkTS bug fix into Fixit instance/class method hooks, use the invocation-scoped ArkTS Proxy, author declarative Component DSL rules, resolve full OHM target paths, or validate a dynamically delivered patch against OhosPatch runtime constraints.
---

# OhosPatch Patch Authoring

Create a standalone JavaScript patch that matches the installed OhosPatch Runtime API and changes no business source merely to enable patching.

## Gather Context

1. Read the installed Skill's `references/fixit.d.js` completely for the public Patch context API.
2. Determine what to patch from the current change set. By default, inspect the working-tree changes and the diff between the current branch and `main` (`git diff main --stat`, `git diff main`, `git status`) and infer the patch target and intent from those edits. Only skip this when the user explicitly names a class, component, bug, or desired behavior change.
3. Read the affected ArkTS class or component and every directly used type. Confirm method names, static/instance ownership, argument order, return shape, and nested property names.
4. Resolve the exact `bundleName`, HarmonyOS `moduleName`, OH package name, source path, and export name per the OHM path format in the Runtime Reference below. Pass the full OHM path directly to `Fixit.fix()` or `Fixit.component()`.
5. When behavior is ambiguous or the declaration and Runtime version differ, inspect `ohospatch/src/main/cpp/runtime/fixit.js` in the OhosPatch source tree if it is available; otherwise rely on `references/fixit.d.js`.

Do not invent business APIs or infer generated ArkUI method names from source syntax alone. Inspect generated ArkTS output when changing Component behavior beyond an established pattern.

## Author Method Patches

Use a plain script with no imports or package loader assumptions:

```js
/// <reference path="./fixit.d.js" />

(function (Fixit) {
  var fix = Fixit.fix(
    'com.example.app/entry/src/main/ets/model/Target#Target'
  );

  var origin = fix.instanceMethod('methodName', function (value) {
    if (value == null) {
      this.state.message = 'fixed';
      return this.state.fallback;
    }
    return origin.apply(this, arguments);
  });
})(Fixit);
```

- Use `instanceMethod` for prototype methods and `classMethod` for static methods.
- Keep the returned original proxy in a local variable when any normal path must retain original behavior.
- Access the real ArkTS receiver through normal dot syntax. Nested reads, writes, method calls, Proxy arguments, and Proxy returns preserve object identity.
- Use the Proxy only during the synchronous handler or `origin` call. Never retain it in globals, timers, promises, or microtasks.
- Treat ordinary handler arguments and newly created JS objects as JSON wire values by default. Nested functions may cross only as retained callbacks when passed to ArkTS Proxy methods or static Component `attr/create` arguments; do not depend on symbols, BigInt, cycles, controllers, or arbitrary native objects crossing as ordinary values.
- Return a value compatible with the original ArkTS method contract.
- Use `Fixit.import(fullPath)` when the Patch must construct another exported ArkTS class or call its static/instance methods. The imported class and every object it returns are synchronous host-VM Proxies; do not use JavaScript `import()` syntax.
- `require(fullPath)` is an alias of `Fixit.import(fullPath)`. Do not use it to prepare Hook targets; pass the path directly to `Fixit.fix()` or `Fixit.component()`.
- Imported Proxies remain valid until `OhosPatch.clear()` or the next Patch installation. They can cross the bridge as method arguments, property values, and Patch results, but newly created plain JS values still follow the JSON wire rules except for retained nested callback functions.
- Use `$r('app.string.name')`, `$r('app.color.name')`, `$r('app.float.name')`, `$r('app.media.name')`, or `Fixit.resource('app.string.name')` when a Patch needs host APP resources. This matches ArkUI native resource path style and requires the host to call `OhosPatch.init(context)` before installing scripts. Shortcut helpers such as `$r.string(name)` still exist, but prefer the native-style path form. Use `Fixit.runtimeInfo()` for system/API/app version branching.

## Author Component Patches

Use the same DSL for supported API 20 state-management V1 and V2 components:

```js
var panel = Fixit.component(
  'com.example.app/entry/src/main/ets/components/Panel#Panel'
);

panel.param('title', 'fixed');
panel.state('count', function (originValue) {
  this.status = 'state patched';
  return originValue < 0 ? 0 : originValue;
});
var originClick = panel.node({ type: 'Button', occurrence: 0 })
  .attrs({ height: 48, backgroundColor: '#1677FF' })
  .event('onClick', function () {
    this.count = this.count + 1;
    return originClick.apply(this, arguments);
  });
```

- Target an exported custom component.
- Register parameter and state replacements directly with `param/state(name, value)` or `param/state(name, function (originValue) { ... })`. The function form receives the original value and binds `this` to the current Component instance Proxy. This is the only public value-replacement DSL.
- Prefer a built-in type string or `{ type, occurrence }`: occurrence is zero-based within that built-in type and avoids attribute capture and deep comparison. Use `{ type, where: { attribute: fixedValue } }` when dynamic branches or lists make occurrence unstable. Every `where` entry matches the original ArkUI attribute method's first argument by JSON value, all entries must match, and only the first matching node is selected. Attribute order does not matter. Values must be compile-time fixed and JSON-serializable; `where` and `occurrence` cannot be combined.
- To patch ArkUI node initialization arguments such as `Column({ space: 14 })`, select the built-in node by `occurrence` and call `.create(...)`, for example `panel.node({ type: 'Column', occurrence: 0 }).create({ space: 20 })`. Avoid `where` for create patches because `where` is resolved from later attribute calls after `create` has already run.
- To patch a parameter passed from a specific parent to a child custom Component, use `Fixit.component(parentFullPath).node({ type: childFullPath, occurrence }).param(name, valueOrHandler)`. The child selector `type` must be the child's full Component path. It only patches the child instance created under that parent occurrence; other child instances elsewhere are unaffected. Child selectors support `param()` only, not `attr()`, `attrs()`, `event()`, or `where`.
- To patch ArkUI nodes rendered by a child custom Component's trailing closure, call `parent.node({ type: childFullPath, occurrence }).builder(nodeSelector)`. ArkTS trailing closure syntax allows one parameterless `@BuilderParam`, so its property name may be omitted.
- When a child receives its Builder explicitly, prefer `builder(builderParamName, nodeSelector)`. The name is required to distinguish multiple `@BuilderParam` properties, and each named BuilderParam has an independent per-type occurrence scope. Builder callback arguments and return values are forwarded unchanged.
- For nested custom components, built-in node `occurrence` is counted inside the target component being patched; child component internals have their own `Fixit.component(childFullPath)` rule. `Column`, `Row`, and `Stack` inside the same component do not reset occurrence. For conditional rendering, only the active `if`/`else` branch contributes nodes on that render. For loops or list builders, every executed iteration contributes in execution order.
- Keep `where` selector values, replacement values, and dynamic attribute-handler return values JSON-serializable. Static `attr(...)` and `create(...)` arguments may contain nested functions such as Alert/Menu/Popup button actions; OhosPatch retains those functions in the Patch JSVM and exposes them to ArkTS as callbacks with `this` bound to the current Component instance proxy.
- Use normal `function` syntax when a Component event patch needs `this`; it is bound to the current Component instance proxy.
- Event handlers receive every JSON-serializable ArkUI event argument in order. `node.event(...)` returns the original callback proxy; call it with `origin.apply(this, arguments)` to forward all arguments and preserve original behavior.
- Wrap `origin.apply(this, arguments)` in `try/catch` when the patch is intended to recover from an original ArkTS exception. OhosPatch converts that explicit origin-call exception into a JSVM `Error`; uncaught origin-call errors fall back to the original ArkTS behavior.
- For ComponentV2, `param()` targets `@Param` and `state()` targets observable instance state such as `@Local`; OhosPatch selects the V1/V2 adapter automatically.
- Do not generate `before`, `after`, `around`, route interception, hierarchy selectors, arbitrary `Resource`/Controller object bridging, or forced refresh logic. Use `$r` for concrete resource values; an executed `id(...)` is a supported `where` attribute.

## Runtime Reference

The Skill is self-contained: the API declaration lives at `references/fixit.d.js` and the rules below replace any need for the OhosPatch `README.md`, which is not installed alongside the Skill.

### OHM target path

Pass the full OHM source path directly to `Fixit.fix()` or `Fixit.component()`:

```text
bundleName/moduleName/[packageName/]src/main/ets/File#ExportName
```

- `@bundle:` prefix and `.ets`/`.ts` suffix are accepted.
- `ExportName` defaults to the file name when omitted.
- When `useNormalizedOHMUrl` is enabled and the oh-package `name` differs from `moduleName`, the `packageName` segment is required; omit it when they match.
- Scoped OH package names are supported. For `@google/somelib`, write `com.example.app/entry/@google/somelib/src/main/ets/foo/Bar#Bar`; the runtime keeps `@google/somelib` as the package path.
- Example: `com.example.app/entry/src/main/ets/model/DemoViewModel#DemoViewModel` resolves to `modulePath = entry/src/main/ets/model/DemoViewModel`, `moduleInfo = com.example.app/entry`, `exportName = DemoViewModel`.

### Current limitations

- Prototype hooks do not cover constructors, instance-field arrow functions, private members, or call sites that bypass property lookup.
- The handler `this` Proxy is valid only for the current synchronous invocation or `origin` call; it must not escape to timers, promises, or globals. `Fixit.import()` Proxies persist until `OhosPatch.clear()` or patch replacement.
- Component DSL supports API 20 state-management V1 and V2 exported custom components, preferred `type + occurrence` selection, `type + where` original-attribute selection, create/attribute argument replacement with nested function callbacks, `$r` resource values, and synchronous event replacement. Not supported: `before`/`after`/`around` event composition, non-exported `@Entry` route pages, hierarchy selectors, arbitrary `Resource`/Controller object bridging, and forced refresh of mounted components.
- At most 256 active timers per runtime; `setInterval(..., 0)` schedules at 1 ms.
- At most 512 deduped dynamic-import class, instance, method, or nested-object handles per patch.
- Download, signature verification, version matching, rollout, caching, rollback, timeout, and circuit breaking are host responsibilities; the HAR owns none of them.

## Validate

1. Run `node --check <patch-file>`.
2. When TypeScript is available, run `tsc --allowJs --checkJs --noEmit --skipLibCheck <fixit.d.js> <patch-file>`, pointing at the Skill's `references/fixit.d.js`.
3. Check that every target path names a real exported class or component and that no duplicate hook or Component rule exists.
4. Exercise both the repaired path and the original path. Verify original method exceptions and return types where relevant.
5. For Component rules, build the HAP and verify visible state and callbacks on an emulator.
6. Keep download, signature verification, version policy, rollout, caching, and rollback in the host APP. The Patch and HAR must not own those policies.

## Deliver

- Produce a complete dynamically deliverable JavaScript file, not a fragment.
- Preserve the declaration reference as the first line for editor completion; it is a comment and does not affect Runtime execution. The declaration ships at `references/fixit.d.js` in the installed Skill—copy it next to the patch file or point the reference at that path.
- State any unresolved target path, type, or Runtime capability explicitly instead of fabricating an implementation.
- Do not modify business classes to opt into patch dispatch unless the user separately requests a Demo fixture.
