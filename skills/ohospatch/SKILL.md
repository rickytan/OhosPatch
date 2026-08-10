---
name: ohospatch
description: Write, review, and repair OhosPatch JavaScript patch scripts for HarmonyOS/OpenHarmony applications. Use when an AI agent needs to translate an ArkTS bug fix into Fixit instance/class method hooks, use the invocation-scoped ArkTS Proxy, author declarative Component DSL rules, resolve full OHM target paths, or validate a dynamically delivered patch against OhosPatch runtime constraints.
---

# OhosPatch Patch Authoring

Create a standalone JavaScript patch that matches the installed OhosPatch Runtime API and changes no business source merely to enable patching.

## Gather Context

1. Read the target project's `fixit.d.js` completely for the public Patch context API. If it is unavailable, use the installed Skill's `references/fixit.d.js` snapshot.
2. Read the affected ArkTS class or component and every directly used type. Confirm method names, static/instance ownership, argument order, return shape, and nested property names.
3. Resolve the exact `bundleName`, HarmonyOS `moduleName`, OH package name, source path, and export name. Pass the full OHM path directly to `Fixit.fix()` or `Fixit.component()`.
4. Read the OhosPatch `README.md` when the task involves module resolution, Component DSL support, Proxy lifetime, deployment, or current limitations.
5. Inspect `ohospatch/src/main/cpp/runtime/fixit.js` in the OhosPatch source tree only when behavior is ambiguous or the declaration and Runtime version differ.

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
- Treat ordinary handler arguments and newly created JS objects as JSON wire values. Do not depend on functions, symbols, BigInt, cycles, controllers, or arbitrary native objects crossing as ordinary values.
- Return a value compatible with the original ArkTS method contract.
- Use `Fixit.import(fullPath)` when the Patch must construct another exported ArkTS class or call its static/instance methods. The imported class and every object it returns are synchronous host-VM Proxies; do not use JavaScript `import()` syntax.
- `require(fullPath)` is only a compatibility alias of `Fixit.import(fullPath)`. Do not use it to obtain Hook target descriptors; pass the path directly to `Fixit.fix()` or `Fixit.component()`.
- Imported Proxies remain valid until `OhosPatch.clear()` or the next Patch installation. They can cross the bridge as method arguments, property values, and Patch results, but newly created plain JS values still follow the JSON wire rules.

## Author Component Patches

Use only currently supported API 20 state-management V1 behavior:

```js
var panel = Fixit.component(
  'com.example.app/entry/src/main/ets/components/Panel#Panel'
);

panel.param('title').replace('fixed');
panel.state('count').transform(function (value) {
  return value < 0 ? 0 : value;
});
panel.node({ type: 'Button', occurrence: 0 })
  .attrs({ height: 48, backgroundColor: '#1677FF' })
  .event('onClick', {
    mode: 'replace',
    capture: ['count'],
    handler: function (_event, context) {
      context.setState({ count: context.state.count + 1 });
    }
  });
```

- Target an exported custom component.
- Select nodes only by built-in type plus zero-based occurrence.
- Keep attribute arguments and replacement values JSON-serializable.
- Use synchronous event mode `replace`; capture at most 16 properties.
- Do not generate `before`, `after`, `around`, route interception, V2 state, resource/controller values, ID/hierarchy selectors, or forced refresh logic.

## Validate

1. Run `node --check <patch-file>`.
2. When TypeScript is available, run `tsc --allowJs --checkJs --noEmit --skipLibCheck fixit.d.js <patch-file>` from the repository root.
3. Check that every target path names a real exported class or component and that no duplicate hook or Component rule exists.
4. Exercise both the repaired path and the original path. Verify original method exceptions and return types where relevant.
5. For Component rules, build the HAP and verify visible state and callbacks on an emulator.
6. Keep download, signature verification, version policy, rollout, caching, and rollback in the host APP. The Patch and HAR must not own those policies.

## Deliver

- Produce a complete dynamically deliverable JavaScript file, not a fragment.
- Preserve the declaration reference as the first line for editor completion; it is a comment and does not affect Runtime execution.
- State any unresolved target path, type, or Runtime capability explicitly instead of fabricating an implementation.
- Do not modify business classes to opt into patch dispatch unless the user separately requests a Demo fixture.
