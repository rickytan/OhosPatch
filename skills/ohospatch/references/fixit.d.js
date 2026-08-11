/* eslint-disable no-unused-vars */

/**
 * OhosPatch JavaScript context declarations.
 *
 * This file is for editor and IDE language services only. It is not loaded by
 * OhosPatch at runtime. Reference it from a patch script with:
 *
 *   /// <reference path="./fixit.d.js" />
 *
 * @version 1.6.0
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
 * @property {string=} packageName Parsed OH package name.
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
 * @typedef {Object} OhosPatchNodeSelector
 * @property {string} type Built-in ArkUI component type, such as `Button`.
 * @property {number=} occurrence Zero-based occurrence; defaults to `0`.
 */

/**
 * @typedef {Object} OhosPatchComponentEventContext
 * @property {Record<string, any>} state Snapshot of properties listed by `capture`.
 * @property {(patch: Record<string, any>) => void} setState Writes captured component properties.
 */

/**
 * @callback OhosPatchComponentEventHandler
 * @this {any} Synchronous Proxy for the current declarative Component instance.
 * @param {...any} args Original ArkUI event arguments followed by an
 *   `OhosPatchComponentEventContext` as the final argument.
 * @returns {any}
 */

/**
 * Callable proxy for the original ArkUI event callback.
 *
 * @typedef {(this: any, ...args: any[]) => any} OhosPatchOriginalEvent
 */

/**
 * Only synchronous `replace` mode is currently supported and at most 16
 * properties may be captured.
 *
 * @typedef {Object} OhosPatchComponentEventRule
 * @property {'replace'=} mode
 * @property {Array<string>=} capture
 * @property {OhosPatchComponentEventHandler} handler
 */

/**
 * @typedef {Object} OhosPatchComponentValueFix
 * @property {(handler: (value: any) => any) => OhosPatchComponentFix} transform Transform the incoming value.
 * @property {(value: OhosPatchJsonValue) => OhosPatchComponentFix} replace Replace with a JSON value.
 */

/**
 * @typedef {(this: any) => OhosPatchJsonValue} OhosPatchAttrHandler
 *   Attribute resolver invoked on each render with `this` bound to the current component instance
 *   proxy; the return value is applied as the attribute argument.
 */

/**
 * @typedef {Object} OhosPatchComponentNodeFix
 * @property {(attributeName: string, value: OhosPatchJsonValue | OhosPatchAttrHandler,
 *   ...args: OhosPatchJsonValue[]) => OhosPatchComponentNodeFix} attr
 *   Override one node attribute. When `value` is a function it is invoked with `this` bound to the
 *   current component instance on each render and its return value is applied as the single attribute
 *   argument; otherwise the JSON arguments are applied verbatim. At least one argument is required.
 * @property {(attributes: Record<string, OhosPatchJsonValue | OhosPatchAttrHandler>) =>
 *   OhosPatchComponentNodeFix} attrs
 *   Override multiple single-argument attributes. Each value may be a JSON value or a resolver function.
 * @property {(eventName: string, rule: OhosPatchComponentEventRule | OhosPatchComponentEventHandler) =>
 *   OhosPatchOriginalEvent} event Replace a synchronous node event callback and return the original callback proxy.
 */

/**
 * @typedef {Object} OhosPatchComponentFix
 * @property {OhosPatchTarget} target
 * @property {(propertyName: string) => OhosPatchComponentValueFix} param Patch an incoming parameter.
 * @property {(propertyName: string) => OhosPatchComponentValueFix} state Patch observable state.
 * @property {(selector: string | OhosPatchNodeSelector) => OhosPatchComponentNodeFix} node Select an ArkUI node.
 */

/** Registers method replacements for one exported ArkTS class. */
class Fixit {
  /**
   * @param {string | OhosPatchTarget} target Full OHM class path, registered alias, or target descriptor.
   * @param {string=} modulePath Legacy normalized module path when `target` is a class name.
   * @param {string=} exportName Legacy export name.
   */
  constructor(target, modulePath, exportName) {
    /** @type {OhosPatchTarget} */
    this.target = /** @type {any} */ (undefined);
  }

  /** @readonly @type {string} */
  static runtimeVersion = '1.6.0';

  /**
   * @param {string | OhosPatchTarget} target Full OHM class path, registered alias, or target descriptor.
   * @param {string=} modulePath
   * @param {string=} exportName
   * @returns {Fixit}
   */
  static fix(target, modulePath, exportName) {
    return /** @type {any} */ (undefined);
  }

  /**
   * @param {string | OhosPatchTarget} target Full OHM component path, registered alias, or target descriptor.
   * @param {string=} modulePath
   * @param {string=} exportName
   * @returns {OhosPatchComponentFix}
   */
  static component(target, modulePath, exportName) {
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

  /**
   * @param {string} className
   * @param {OhosPatchTarget} target
   * @returns {void}
   */
  static registerTarget(className, target) {}

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
 * `bundleName/moduleName/[packageName/]src/main/ets/File#ExportName`.
 *
 * @param {string} fullPath
 * @returns {OhosPatchImportedClass}
 */
function require(fullPath) {
  return Fixit.import(fullPath);
}

/** @type {null} */
var nil = null;

/** @type {null} */
var Nil = null;

/** @param {any} value @returns {boolean} */
function isNil(value) {
  return false;
}

/** @param {any} value @returns {any} */
function nilToNull(value) {
  return /** @type {any} */ (undefined);
}

/** @param {any} value @returns {any} */
function nullToNil(value) {
  return /** @type {any} */ (undefined);
}

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
