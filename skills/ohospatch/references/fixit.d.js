/* eslint-disable no-unused-vars */

/**
 * OhosPatch JavaScript context declarations.
 *
 * This file is for editor and IDE language services only. It is not loaded by
 * OhosPatch at runtime. Reference it from a patch script with:
 *
 *   /// <reference path="./fixit.d.js" />
 *
 * @version 1.16.0
 */

/**
 * A JSON-compatible value. JSDoc cannot express this recursive union without
 * producing false-positive circular alias errors in some editors.
 *
 * @typedef {any} OhosPatchJsonValue
 */

/**
 * A HarmonyOS module export resolved by OhosPatch in the ArkTS VM.
 *
 * @typedef {Object} OhosPatchTarget
 * @property {string} className Exported class name used in diagnostics.
 * @property {string} modulePath Normalized OHM package source path.
 * @property {string=} moduleInfo `bundleName/moduleName` passed to N-API.
 * @property {string=} exportName Export name; defaults to `className`.
 * @property {string=} bundleName Parsed bundle name.
 * @property {string=} moduleName Parsed HarmonyOS module name.
 */

/**
 * Replacement implementation for an ArkTS instance or class method.
 * `this` is a synchronous Proxy for the original ArkTS receiver.
 *
 * Ordinary objects in `args` are JSON values.
 *
 * @typedef {(this: any, ...args: any[]) => any} OhosPatchMethodHandler
 */

/**
 * Callable proxy for the saved original ArkTS implementation.
 *
 * ArkTS object results remain proxied.
 *
 * @typedef {(this: any, ...args: any[]) => any} OhosPatchOriginalMethod
 */

/**
 * A callable and constructable ArkTS class Proxy returned by `Fixit.import()`.
 * Its concrete static and instance members depend on the imported business class.
 *
 * @typedef {any} OhosPatchImportedClass
 */

/**
 * Runtime metadata exposed to Patch scripts.
 *
 * @typedef {Object} OhosPatchRuntimeInfo
 * @property {string=} osFullName System OS version string.
 * @property {number=} sdkApiVersion Current SDK API level.
 * @property {number=} firstApiVersion First API level supported by the device.
 * @property {number=} majorVersion System major version.
 * @property {number=} seniorVersion System senior version.
 * @property {number=} featureVersion System feature version.
 * @property {number=} buildVersion System build version.
 * @property {string=} versionId System version ID.
 * @property {string=} buildType System build type.
 * @property {string=} osReleaseType System release type.
 * @property {string=} bundleName Current application bundle name.
 * @property {string=} appVersionName Current application version name.
 * @property {number=} appVersionCode Current application version code.
 * @property {string} patchRuntimeVersion OhosPatch JS runtime version.
 */

/**
 * Resource lookup helper backed by the host app `Context.resourceManager`.
 * The main call form mirrors ArkUI native resources: `$r('app.string.name')`,
 * `$r('app.color.name')`, `$r('app.float.name')`, `$r('app.media.name')`.
 *
 * @typedef {((path: string, ...args: Array<string | number>) => any) & {
 *   string(name: string, ...args: Array<string | number>): string,
 *   color(name: string): number,
 *   number(name: string): number,
 *   float(name: string): number,
 *   integer(name: string): number,
 *   int(name: string): number,
 *   stringArray(name: string): string[],
 *   media(name: string, density?: number): string,
 *   image(name: string, density?: number): string
 * }} OhosPatchResource
 */

/**
 * @typedef {Object} OhosPatchNodeSelector
 * @property {string} type Built-in ArkUI component type, such as `Button`, or a full custom Component
 *   path when patching a child Component creation point.
 * @property {number=} occurrence Zero-based occurrence within the actually executed render path for this node type; defaults to `0`. Cannot be combined with `where`.
 * @property {Record<string, OhosPatchJsonValue>=} where Non-empty map of original, compile-time-fixed ArkUI attribute values. All entries must match; the first matching node is selected. Cannot be combined with `occurrence`.
 */

/**
 * @callback OhosPatchComponentEventHandler
 * @this {any} Synchronous Proxy for the current declarative Component instance.
 * @param {...any} args JSON-serializable snapshots of the original ArkUI event arguments, in order.
 * @returns {any}
 */

/**
 * Callable proxy for the original ArkUI event callback.
 *
 * @typedef {(this: any, ...args: any[]) => any} OhosPatchOriginalEvent
 */

/**
 * @callback OhosPatchComponentValueHandler
 * @this {any} Synchronous Proxy for the current declarative Component instance.
 * @param {any} originValue Current parameter or state value before replacement.
 * @returns {any} Replacement parameter or state value.
 */

/**
 * @typedef {(this: any) => OhosPatchJsonValue} OhosPatchAttrHandler
 *   Attribute resolver invoked on each render with `this` bound to the current component instance
 *   proxy; the return value is applied as the attribute argument.
 */

/**
 * @typedef {Object} OhosPatchComponentBuilderNodeFix
 * @property {(...args: OhosPatchJsonValue[]) => OhosPatchComponentBuilderNodeFix} create
 *   Replace the selected ArkUI node's initialization call (`create` / `createWithLabel`) inside BuilderParam
 *   content. Arguments are applied before the original builder's later attributes and events run.
 * @property {(attributeName: string, value: OhosPatchJsonValue | OhosPatchAttrHandler,
 *   ...args: OhosPatchJsonValue[]) => OhosPatchComponentBuilderNodeFix} attr
 *   Override one ArkUI node attribute inside a named BuilderParam passed to a selected child custom Component.
 *   Handler `this` is bound to the parent component instance.
 * @property {(attributes: Record<string, OhosPatchJsonValue | OhosPatchAttrHandler>) =>
 *   OhosPatchComponentBuilderNodeFix} attrs
 *   Override multiple single-argument attributes inside the selected BuilderParam content.
 * @property {(eventName: string, handler: OhosPatchComponentEventHandler) =>
 *   OhosPatchOriginalEvent} event Replace a synchronous node event callback inside the selected BuilderParam
 *   content and return the original callback proxy. Handler `this` is bound to the parent component instance.
 */

/**
 * @typedef {Object} OhosPatchComponentNodeFix
 * @property {(...args: OhosPatchJsonValue[]) => OhosPatchComponentNodeFix} create
 *   Replace the selected ArkUI node's initialization call (`create` / `createWithLabel`). For example,
 *   `node({ type: 'Column', occurrence: 0 }).create({ space: 20 })` patches `Column({ space: 14 })`.
 *   Use `occurrence` selectors for create patches; `where` selectors are evaluated after create arguments.
 * @property {(attributeName: string, value: OhosPatchJsonValue | OhosPatchAttrHandler,
 *   ...args: OhosPatchJsonValue[]) => OhosPatchComponentNodeFix} attr
 *   Override one node attribute. When `value` is a function it is invoked with `this` bound to the
 *   current component instance on each render and its return value is applied as the single attribute
 *   argument; extra arguments are rejected in handler mode. Otherwise the JSON arguments are applied
 *   verbatim. At least one argument is required.
 * @property {(attributes: Record<string, OhosPatchJsonValue | OhosPatchAttrHandler>) =>
 *   OhosPatchComponentNodeFix} attrs
 *   Override multiple single-argument attributes. Each value may be a JSON value or a resolver function.
 * @property {(eventName: string, handler: OhosPatchComponentEventHandler) =>
 *   OhosPatchOriginalEvent} event Replace a synchronous node event callback and return the original callback proxy.
 * @property {(propertyName: string, replacement: OhosPatchJsonValue | OhosPatchComponentValueHandler) =>
 *   OhosPatchComponentNodeFix} param
 *   Replace an incoming parameter on a selected child custom Component. The selector `type` must be the
 *   child's full Component path. Built-in ArkUI nodes do not support this method.
 * @property {(((selector: string | OhosPatchNodeSelector) => OhosPatchComponentBuilderNodeFix) &
 *   ((builderParamName: string, selector: string | OhosPatchNodeSelector) =>
 *   OhosPatchComponentBuilderNodeFix))} builder
 *   Select an ArkUI node rendered by a BuilderParam passed to the selected child custom Component. Use the
 *   one-argument form for trailing-closure Components with one BuilderParam. Pass the BuilderParam property name
 *   when the child declares multiple BuilderParams. Each named BuilderParam has an independent occurrence scope.
 */

/**
 * @typedef {Object} OhosPatchComponentFix
 * @property {OhosPatchTarget} target
 * @property {(propertyName: string, replacement: OhosPatchJsonValue | OhosPatchComponentValueHandler) =>
 *   OhosPatchComponentFix} param Replace an incoming parameter with a JSON value or derive it from the current value.
 * @property {(propertyName: string, replacement: OhosPatchJsonValue | OhosPatchComponentValueHandler) =>
 *   OhosPatchComponentFix} state Replace observable state with a JSON value or derive it from the current value.
 * @property {(selector: string | OhosPatchNodeSelector) => OhosPatchComponentNodeFix} node Select an ArkUI node by
 *   built-in type plus either zero-based occurrence or original attribute values, or select a child custom
 *   Component by full path plus occurrence. Prefer occurrence for lower runtime overhead.
 */

/** Registers method replacements for one exported ArkTS class. */
class Fixit {
  /**
   * @param {string} target Full OHM class path.
   */
  constructor(target) {
    /** @type {OhosPatchTarget} */
    this.target = /** @type {any} */ (undefined);
  }

  /** @readonly @type {string} */
  static runtimeVersion = '1.16.0';

  /**
   * @param {string} target Full OHM class path.
   * @returns {Fixit}
   */
  static fix(target) {
    return /** @type {any} */ (undefined);
  }

  /**
   * Target an exported API 20 state-management V1 or V2 component. The Runtime selects the adapter automatically.
   * @param {string} target Full OHM component path.
   * @returns {OhosPatchComponentFix}
   */
  static component(target) {
    return /** @type {any} */ (undefined);
  }

  /**
   * Synchronously load an exported ArkTS class in the host VM. The returned
   * Proxy supports static members, construction, instance members, and nested
   * object results. Imported Proxies remain valid until `OhosPatch.clear()` or
   * the next Patch installation.
   *
   * @param {string} fullPath
   * @returns {OhosPatchImportedClass}
   */
  static import(fullPath) {
    return /** @type {any} */ (undefined);
  }

  /** @type {OhosPatchResource} */
  static resource = /** @type {any} */ (undefined);

  /**
   * @returns {OhosPatchRuntimeInfo}
   */
  static runtimeInfo() {
    return /** @type {any} */ (undefined);
  }

  /**
   * @param {string} methodName
   * @param {OhosPatchMethodHandler} handler
   * @returns {OhosPatchOriginalMethod}
   */
  instanceMethod(methodName, handler) {
    return /** @type {any} */ (undefined);
  }

  /**
   * @param {string} methodName
   * @param {OhosPatchMethodHandler} handler
   * @returns {OhosPatchOriginalMethod}
   */
  classMethod(methodName, handler) {
    return /** @type {any} */ (undefined);
  }
}

/**
 * Alias of `Fixit.import()`. Synchronously load an exported ArkTS class from
 * `bundleName/moduleName/[packagePath/]src/main/ets/File#ExportName`,
 * `@package/name/src/main/ets/File#ExportName`, or
 * `/src/main/ets/File#ExportName`.
 * The shorthand forms use the current host bundle/module captured by
 * `OhosPatch.init(context)`.
 *
 * @param {string} fullPath
 * @returns {OhosPatchImportedClass}
 */
function require(fullPath) {
  return Fixit.import(fullPath);
}

/** @type {OhosPatchResource} */
var $r = Fixit.resource;

/** @typedef {(...args: any[]) => void} OhosPatchTimerCallback */

/**
 * @param {OhosPatchTimerCallback} callback
 * @param {number=} delay
 * @param {...any} args
 * @returns {number}
 */
function setTimeout(callback, delay, ...args) {
  return 0;
}

/** @param {number} id @returns {void} */
function clearTimeout(id) {}

/**
 * A zero-millisecond interval is normalized to one millisecond.
 *
 * @param {OhosPatchTimerCallback} callback
 * @param {number=} delay
 * @param {...any} args
 * @returns {number}
 */
function setInterval(callback, delay, ...args) {
  return 0;
}

/** @param {number} id @returns {void} */
function clearInterval(id) {}

/**
 * Schedule a callback after the current turn.
 *
 * @param {OhosPatchTimerCallback} callback
 * @param {...any} args
 * @returns {number}
 */
function setImmediate(callback, ...args) {
  return 0;
}

/** @param {number} id @returns {void} */
function clearImmediate(id) {}

/**
 * @param {() => void} callback
 * @returns {void}
 */
function queueMicrotask(callback) {}

/**
 * The shape of the standard `console` global in the Patch context. Each method
 * accepts any number of values and forwards formatted output to HarmonyOS HiLog.
 *
 * @typedef {Object} OhosPatchConsole
 * @property {(...args: any[]) => void} debug
 * @property {(...args: any[]) => void} log
 * @property {(...args: any[]) => void} info
 * @property {(...args: any[]) => void} warn
 * @property {(...args: any[]) => void} error
 */
