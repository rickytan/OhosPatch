# Changelog

## 1.0.3

- Added parent-scoped patching for custom Component `@BuilderParam` and trailing-closure content through `node(...).slot(...)`.
- Added slot node attribute and event patches for both method-based builders and inline trailing closures, with event `this` bound to the parent Component instance.
- Applied initial Component state patches before `finalizeConstruction` to avoid render-time state mutation warnings while preserving ComponentV2 reuse behavior.
- Added real HarmonyOS runtime coverage for ComponentV1, ComponentV2, custom child Components, and nested slot content.

## 1.0.2

- Added more actionable OhosPatch runtime diagnostics for module, export, method, and Component patch failures.
- Documented that only exported ArkTS classes, functions, and custom Components can be patched.
- Switched the built-in Patch Runtime rawfile minification to the Gulp/Terser pipeline.
- Kept `libc++_shared.so` in Debug test builds while excluding it from Release HAR artifacts.

## 1.0.1

- Added rawfile-based OhosPatch runtime packaging with minified JavaScript.
- Improved native module loading with runtime bundle and module information.
- Reduced native binary size by enabling hidden visibility and linker stripping.

## 1.0.0

- Initial production release of OhosPatch.
- Added JSVM-based patch runtime for HarmonyOS/OpenHarmony ArkTS apps.
- Added class instance method patching.
- Added declarative ArkUI Component and ComponentV2 patch DSL.
- Added runtime helpers including console logging, import/require, timers, and native module bridging.
