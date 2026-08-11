(function (global) {
  'use strict';

  var registry = {
    instance: Object.create(null),
    klass: Object.create(null),
    uiValues: Object.create(null),
    uiAttrs: Object.create(null),
    uiEvents: Object.create(null)
  };
  var specs = [];
  var uiSpecs = [];
  var uiRuleKeys = Object.create(null);
  var nextUiRuleId = 1;
  var targets = Object.create(null);
  var timers = Object.create(null);
  var nextTimerId = 1;
  var nativeProxyMetadata = new WeakMap();
  var nativeProxyCache = Object.create(null);
  var importedProxyMetadata = new WeakMap();
  var importedProxyCache = Object.create(null);

  var REMOTE_HANDLE_KEY = '__ohospatch_proxy_handle__';
  var IMPORT_HANDLE_KEY = '__ohospatch_import_handle__';
  var UNDEFINED_VALUE_KEY = '__ohospatch_proxy_undefined__';
  var UI_EVENT_CONTEXT_KEY = '__ohospatch_ui_event_context__';

  function own(object, property) {
    return Object.prototype.hasOwnProperty.call(object, property);
  }

  function key(targetKey, methodName) {
    return targetKey + '#' + methodName;
  }

  function bucket(isClassMethod) {
    return isClassMethod ? registry.klass : registry.instance;
  }

  function encodeNativeWireValue(value, ancestors) {
    if ((typeof value === 'object' && value !== null) || typeof value === 'function') {
      var metadata = nativeProxyMetadata.get(value);
      if (metadata) {
        var reference = {};
        reference[REMOTE_HANDLE_KEY] = metadata.handle;
        return reference;
      }
      metadata = importedProxyMetadata.get(value);
      if (metadata) {
        var importedReference = {};
        importedReference[IMPORT_HANDLE_KEY] = metadata.handle;
        return importedReference;
      }
    }
    if (value === undefined) {
      var undefinedValue = {};
      undefinedValue[UNDEFINED_VALUE_KEY] = true;
      return undefinedValue;
    }
    if (value === null || typeof value === 'string' || typeof value === 'boolean') {
      return value;
    }
    if (typeof value === 'number') {
      return Number.isFinite(value) ? value : null;
    }
    if (typeof value !== 'object') {
      throw new TypeError('OhosPatch wire value contains an unsupported type');
    }
    if (ancestors.indexOf(value) !== -1) {
      throw new TypeError('OhosPatch wire value contains a circular reference');
    }

    ancestors.push(value);
    var encoded;
    if (Array.isArray(value)) {
      encoded = value.map(function (item) {
        return encodeNativeWireValue(item, ancestors);
      });
    } else {
      encoded = {};
      Object.keys(value).forEach(function (property) {
        encoded[property] = encodeNativeWireValue(value[property], ancestors);
      });
    }
    ancestors.pop();
    return encoded;
  }

  function encodeNativeWire(value) {
    return JSON.stringify(encodeNativeWireValue(value, []));
  }

  function decodeNativeResponse(response, receiverHandle) {
    if (!Array.isArray(response) || typeof response[0] !== 'string') {
      throw new Error('Invalid OhosPatch native proxy response');
    }
    if (response[0] === 'value') {
      return response[1];
    }
    if (response[0] === 'undefined') {
      return undefined;
    }
    if (response[0] === 'object') {
      return makeNativeProxy(response[1], false, response[1]);
    }
    if (response[0] === 'function') {
      return makeNativeProxy(response[1], true, receiverHandle);
    }
    if (response[0] === 'ok') {
      return true;
    }
    throw new Error(response[1] || 'OhosPatch native proxy operation failed');
  }

  function makeNativeProxy(handle, callable, receiverHandle) {
    var cacheKey = callable ? 'f:' + handle + ':' + receiverHandle : 'o:' + handle;
    if (own(nativeProxyCache, cacheKey)) {
      return nativeProxyCache[cacheKey];
    }

    var target = callable ? function () {} : {};
    var proxy = new Proxy(target, {
      get: function (localTarget, property) {
        if (typeof property === 'symbol') {
          if (property === Symbol.toStringTag) {
            return 'OhosPatchProxy';
          }
          if (property === Symbol.iterator) {
            return function () {
              var index = 0;
              return {
                next: function () {
                  var length = proxy.length;
                  return index < length ? { value: proxy[index++], done: false } : { done: true };
                }
              };
            };
          }
          return undefined;
        }
        var descriptor = Object.getOwnPropertyDescriptor(localTarget, property);
        if (descriptor && descriptor.configurable === false) {
          return localTarget[property];
        }
        return decodeNativeResponse(global.__ohospatch_proxyGet(handle, String(property)), handle);
      },
      set: function (_localTarget, property, value) {
        if (typeof property === 'symbol') {
          throw new TypeError('OhosPatch cannot assign a symbol property');
        }
        var wire;
        try {
          wire = encodeNativeWire(value);
        } catch (_) {
          throw new TypeError('OhosPatch proxy assignment must be bridge-serializable');
        }
        return decodeNativeResponse(global.__ohospatch_proxySet(handle, String(property), wire), handle);
      },
      apply: function () {
        var args = Array.prototype.slice.call(arguments[2]);
        var wire;
        try {
          wire = encodeNativeWire(args);
        } catch (_) {
          throw new TypeError('OhosPatch proxy method arguments must be bridge-serializable');
        }
        return decodeNativeResponse(
          global.__ohospatch_proxyCall(handle, receiverHandle, wire),
          receiverHandle
        );
      }
    });
    nativeProxyMetadata.set(proxy, { handle: handle, receiverHandle: receiverHandle, callable: callable });
    nativeProxyCache[cacheKey] = proxy;
    return proxy;
  }

  function decodeImportedResponse(response, receiverHandle) {
    if (!Array.isArray(response) || typeof response[0] !== 'string') {
      throw new Error('Invalid OhosPatch imported proxy response');
    }
    if (response[0] === 'value') {
      return response[1];
    }
    if (response[0] === 'undefined') {
      return undefined;
    }
    if (response[0] === 'object') {
      return makeImportedProxy(response[1], false, response[1]);
    }
    if (response[0] === 'function') {
      return makeImportedProxy(response[1], true, receiverHandle);
    }
    if (response[0] === 'ok') {
      return true;
    }
    throw new Error(response[1] || 'OhosPatch imported proxy operation failed');
  }

  function makeImportedProxy(handle, callable, receiverHandle) {
    var cacheKey = callable ? 'f:' + handle + ':' + receiverHandle : 'o:' + handle;
    if (own(importedProxyCache, cacheKey)) {
      return importedProxyCache[cacheKey];
    }

    var target = callable ? function () {} : {};
    var proxy = new Proxy(target, {
      get: function (localTarget, property) {
        if (typeof property === 'symbol') {
          if (property === Symbol.toStringTag) {
            return 'OhosPatchImportedProxy';
          }
          if (property === Symbol.iterator) {
            return function () {
              var index = 0;
              return {
                next: function () {
                  var length = proxy.length;
                  return index < length ? { value: proxy[index++], done: false } : { done: true };
                }
              };
            };
          }
          return undefined;
        }
        var descriptor = Object.getOwnPropertyDescriptor(localTarget, property);
        if (descriptor && descriptor.configurable === false) {
          return localTarget[property];
        }
        return decodeImportedResponse(global.__ohospatch_importGet(handle, String(property)), handle);
      },
      set: function (_localTarget, property, value) {
        if (typeof property === 'symbol') {
          throw new TypeError('OhosPatch cannot assign a symbol property');
        }
        var wire;
        try {
          wire = encodeNativeWire(value);
        } catch (_) {
          throw new TypeError('OhosPatch imported proxy assignment must be bridge-serializable');
        }
        return decodeImportedResponse(global.__ohospatch_importSet(handle, String(property), wire), handle);
      },
      apply: function () {
        var args = Array.prototype.slice.call(arguments[2]);
        var wire;
        try {
          wire = encodeNativeWire(args);
        } catch (_) {
          throw new TypeError('OhosPatch imported method arguments must be bridge-serializable');
        }
        return decodeImportedResponse(
          global.__ohospatch_importCall(handle, receiverHandle, wire),
          receiverHandle
        );
      },
      construct: function () {
        var args = Array.prototype.slice.call(arguments[1]);
        var wire;
        try {
          wire = encodeNativeWire(args);
        } catch (_) {
          throw new TypeError('OhosPatch imported constructor arguments must be bridge-serializable');
        }
        return decodeImportedResponse(global.__ohospatch_importConstruct(handle, wire), handle);
      }
    });
    importedProxyMetadata.set(proxy, { handle: handle, receiverHandle: receiverHandle, callable: callable });
    importedProxyCache[cacheKey] = proxy;
    return proxy;
  }

  function importTarget(fullPath) {
    var target = parseTargetPath(fullPath);
    return decodeImportedResponse(global.__ohospatch_import(JSON.stringify(target)), 0);
  }

  function encodePatchResult(value) {
    var metadata = ((typeof value === 'object' && value !== null) || typeof value === 'function')
      ? nativeProxyMetadata.get(value)
      : null;
    if (metadata) {
      return { kind: 'remote', handle: metadata.handle };
    }
    metadata = ((typeof value === 'object' && value !== null) || typeof value === 'function')
      ? importedProxyMetadata.get(value)
      : null;
    if (metadata) {
      return { kind: 'imported', handle: metadata.handle };
    }
    if (value === undefined) {
      return { kind: 'undefined' };
    }
    var wire;
    try {
      wire = encodeNativeWire(value);
    } catch (_) {
      throw new TypeError('OhosPatch return value must be bridge-serializable');
    }
    return { kind: 'wire', value: JSON.parse(wire) };
  }

  function copyTarget(target) {
    return {
      className: target.className,
      modulePath: target.modulePath,
      moduleInfo: target.moduleInfo || '',
      exportName: target.exportName || target.className,
      bundleName: target.bundleName || '',
      moduleName: target.moduleName || '',
      packageName: target.packageName || ''
    };
  }

  function parseTargetPath(fullPath) {
    if (typeof fullPath !== 'string' || fullPath.trim().length === 0) {
      throw new TypeError('OhosPatch expects a non-empty full module path');
    }

    var path = fullPath.trim();
    if (path.indexOf('@bundle:') === 0) {
      path = path.slice('@bundle:'.length);
    }
    if (path.indexOf('\\') !== -1 || path.charAt(0) === '/' || path.charAt(path.length - 1) === '/') {
      throw new Error('OhosPatch path must use bundleName/moduleName/[packageName/]src/main/ets/File format');
    }

    var hashIndex = path.indexOf('#');
    var exportName = '';
    if (hashIndex !== -1) {
      if (path.indexOf('#', hashIndex + 1) !== -1) {
        throw new Error('OhosPatch path can contain only one export separator (#)');
      }
      exportName = path.slice(hashIndex + 1);
      path = path.slice(0, hashIndex);
    }
    if (/\.(ets|ts)$/.test(path)) {
      path = path.replace(/\.(ets|ts)$/, '');
    }

    var parts = path.split('/');
    var sourceOffset = parts[2] === 'src' ? 2 : 3;
    if (parts.length < sourceOffset + 4 ||
        parts[sourceOffset] !== 'src' ||
        parts[sourceOffset + 1] !== 'main' ||
        parts[sourceOffset + 2] !== 'ets') {
      throw new Error('OhosPatch path must use bundleName/moduleName/[packageName/]src/main/ets/File format');
    }
    for (var index = 0; index < parts.length; index += 1) {
      if (!parts[index]) {
        throw new Error('OhosPatch path contains an empty segment');
      }
    }

    var bundleName = parts[0];
    var moduleName = parts[1];
    var packageName = sourceOffset === 2 ? moduleName : parts[2];
    var fileName = parts[parts.length - 1];
    exportName = exportName || fileName;
    if (!/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(exportName)) {
      throw new Error('OhosPatch export name is invalid: ' + exportName);
    }

    return {
      className: exportName === 'default' ? fileName : exportName,
      modulePath: packageName + '/' + parts.slice(sourceOffset).join('/'),
      moduleInfo: bundleName + '/' + moduleName,
      exportName: exportName,
      bundleName: bundleName,
      moduleName: moduleName,
      packageName: packageName
    };
  }

  function targetKey(target) {
    return target.moduleInfo + '|' + target.modulePath + '#' + target.exportName;
  }

  function normalizeTarget(target, modulePath, exportName) {
    var normalized;
    if (typeof target === 'string') {
      if (!modulePath && own(targets, target)) {
        normalized = copyTarget(targets[target]);
      } else if (!modulePath && (target.indexOf('/') !== -1 || target.indexOf('@bundle:') === 0)) {
        normalized = parseTargetPath(target);
      } else {
        normalized = {
          className: target,
          modulePath: modulePath || '',
          moduleInfo: '',
          exportName: exportName || target
        };
      }
    } else if (target && typeof target === 'object') {
      normalized = copyTarget(target);
    } else {
      throw new TypeError('Fixit.fix requires a class name or target descriptor');
    }

    if (!normalized.className || !normalized.modulePath) {
      throw new Error('Fixit target requires className and modulePath');
    }
    return normalized;
  }

  function validateMethod(methodName, handler) {
    if (typeof methodName !== 'string' || methodName.length === 0) {
      throw new TypeError('Fixit method name must be a non-empty string');
    }
    if (typeof handler !== 'function') {
      throw new TypeError('Fixit method handler must be a function');
    }
  }

  function register(target, methodName, isClassMethod, handler) {
    validateMethod(methodName, handler);
    var identity = targetKey(target);
    var methodKey = key(identity, methodName);
    var methods = bucket(isClassMethod);
    if (own(methods, methodKey)) {
      throw new Error('Duplicate patch for ' + target.className + '.' + methodName);
    }

    methods[methodKey] = handler;
    specs.push({
      className: target.className,
      modulePath: target.modulePath,
      moduleInfo: target.moduleInfo,
      exportName: target.exportName,
      targetKey: identity,
      methodName: methodName,
      classMethod: isClassMethod
    });

    return function () {
      return decodeNativeResponse(
        global.__ohospatch_origin(encodeNativeWire(Array.prototype.slice.call(arguments))),
        0
      );
    };
  }

  function validateUiName(value, label) {
    if (typeof value !== 'string' || !/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(value)) {
      throw new TypeError(label + ' must be a valid identifier');
    }
    return value;
  }

  function copyJsonValue(value, label) {
    if (value === undefined || typeof value === 'function' || typeof value === 'symbol') {
      throw new TypeError(label + ' must be JSON-serializable');
    }
    var json;
    try {
      json = JSON.stringify(value);
    } catch (_) {
      throw new TypeError(label + ' must be JSON-serializable');
    }
    if (json === undefined) {
      throw new TypeError(label + ' must be JSON-serializable');
    }
    return JSON.parse(json);
  }

  function nextRuleId() {
    var id = nextUiRuleId;
    nextUiRuleId += 1;
    return id;
  }

  function registerUiRule(uniqueKey, rule, handlerBucket, handler) {
    if (own(uiRuleKeys, uniqueKey)) {
      throw new Error('Duplicate component patch rule for ' + uniqueKey);
    }
    var ruleId = nextRuleId();
    rule.ruleId = ruleId;
    uiRuleKeys[uniqueKey] = ruleId;
    uiSpecs.push(rule);
    if (handlerBucket) {
      handlerBucket[ruleId] = handler;
    }
    return ruleId;
  }

  function makeUiEventOrigin() {
    return function () {
      var args = Array.prototype.slice.call(arguments);
      var tail = args.length > 0 ? args[args.length - 1] : null;
      if (tail && tail[UI_EVENT_CONTEXT_KEY] === true) {
        args.pop();
      }
      return decodeNativeResponse(global.__ohospatch_eventOrigin(encodeNativeWire(args)), 0);
    };
  }

  function copyUiTarget(target) {
    var descriptor = copyTarget(target);
    descriptor.targetKey = targetKey(target);
    return descriptor;
  }

  function ComponentFix(target, modulePath, exportName) {
    this.target = normalizeTarget(target, modulePath, exportName);
  }

  function ComponentValueFix(component, kind, propertyName) {
    this.component = component;
    this.kind = kind;
    this.propertyName = validateUiName(propertyName, 'Component property name');
  }

  ComponentValueFix.prototype.transform = function (handler) {
    if (typeof handler !== 'function') {
      throw new TypeError('Component value transform must be a function');
    }
    var target = this.component.target;
    var uniqueKey = targetKey(target) + '|' + this.kind + '|' + this.propertyName;
    var rule = copyUiTarget(target);
    rule.kind = this.kind;
    rule.propertyName = this.propertyName;
    rule.operation = 'transform';
    registerUiRule(uniqueKey, rule, registry.uiValues, handler);
    return this.component;
  };

  ComponentValueFix.prototype.replace = function (value) {
    var replacement = copyJsonValue(value, 'Component replacement value');
    return this.transform(function () {
      return replacement;
    });
  };

  function normalizeNodeSelector(selector) {
    var normalized = typeof selector === 'string' ? { type: selector } : selector;
    if (!normalized || typeof normalized !== 'object') {
      throw new TypeError('Component node selector must be a type string or descriptor');
    }
    var type = validateUiName(normalized.type, 'Component node type');
    var occurrence = normalized.occurrence === undefined ? 0 : Number(normalized.occurrence);
    if (!Number.isInteger(occurrence) || occurrence < 0 || occurrence > 4294967295) {
      throw new TypeError('Component node occurrence must be a non-negative uint32 integer');
    }
    return {
      type: type,
      occurrence: occurrence
    };
  }

  function ComponentNodeFix(component, selector) {
    this.component = component;
    this.selector = normalizeNodeSelector(selector);
  }

  ComponentNodeFix.prototype.attr = function (attributeName) {
    var name = validateUiName(attributeName, 'Component attribute name');
    var args = Array.prototype.slice.call(arguments, 1);
    if (args.length === 0) {
      throw new TypeError('Component attribute requires at least one argument');
    }

    var target = this.component.target;
    var selector = this.selector;
    var uniqueKey = targetKey(target) + '|node|' + selector.type + '|' + selector.occurrence + '|attr|' + name;
    var rule = copyUiTarget(target);
    rule.kind = 'attribute';
    rule.nodeType = selector.type;
    rule.occurrence = selector.occurrence;
    rule.attributeName = name;

    if (typeof args[0] === 'function') {
      rule.attrHandler = true;
      registerUiRule(uniqueKey, rule, registry.uiAttrs, args[0]);
    } else {
      rule.attrHandler = false;
      rule.arguments = copyJsonValue(args, 'Component attribute arguments');
      registerUiRule(uniqueKey, rule, null, null);
    }
    return this;
  };

  ComponentNodeFix.prototype.attrs = function (attributes) {
    if (!attributes || typeof attributes !== 'object' || Array.isArray(attributes)) {
      throw new TypeError('Component attributes must be an object');
    }
    var self = this;
    Object.keys(attributes).forEach(function (name) {
      self.attr(name, attributes[name]);
    });
    return this;
  };

  ComponentNodeFix.prototype.event = function (eventName, ruleOrHandler) {
    var name = validateUiName(eventName, 'Component event name');
    var mode = 'replace';
    var capture = [];
    var handler = ruleOrHandler;
    if (ruleOrHandler && typeof ruleOrHandler === 'object') {
      mode = ruleOrHandler.mode || mode;
      capture = ruleOrHandler.capture || capture;
      handler = ruleOrHandler.handler;
    }
    if (mode !== 'replace') {
      throw new Error('Only replace component event mode is currently supported');
    }
    if (typeof handler !== 'function') {
      throw new TypeError('Component event handler must be a function');
    }
    if (!Array.isArray(capture)) {
      throw new TypeError('Component event capture must be an array');
    }
    if (capture.length > 16) {
      throw new RangeError('Component event capture supports at most 16 properties');
    }
    capture = capture.map(function (propertyName) {
      return validateUiName(propertyName, 'Captured component property');
    });

    var target = this.component.target;
    var selector = this.selector;
    var uniqueKey = targetKey(target) + '|node|' + selector.type + '|' + selector.occurrence + '|event|' + name;
    var rule = copyUiTarget(target);
    rule.kind = 'event';
    rule.nodeType = selector.type;
    rule.occurrence = selector.occurrence;
    rule.eventName = name;
    rule.mode = mode;
    rule.capture = capture;
    registerUiRule(uniqueKey, rule, registry.uiEvents, handler);
    return makeUiEventOrigin();
  };

  ComponentFix.prototype.param = function (propertyName) {
    return new ComponentValueFix(this, 'param', propertyName);
  };

  ComponentFix.prototype.state = function (propertyName) {
    return new ComponentValueFix(this, 'state', propertyName);
  };

  ComponentFix.prototype.node = function (selector) {
    return new ComponentNodeFix(this, selector);
  };

  function Fixit(target, modulePath, exportName) {
    if (!(this instanceof Fixit)) {
      return new Fixit(target, modulePath, exportName);
    }
    this.target = normalizeTarget(target, modulePath, exportName);
  }

  Fixit.fix = function (target, modulePath, exportName) {
    return new Fixit(target, modulePath, exportName);
  };

  Fixit.component = function (target, modulePath, exportName) {
    return new ComponentFix(target, modulePath, exportName);
  };

  Fixit.import = importTarget;

  Fixit.registerTarget = function (className, target) {
    if (typeof className !== 'string' || className.length === 0) {
      throw new TypeError('Fixit.registerTarget requires a class name');
    }
    var descriptor = copyTarget(target || {});
    descriptor.className = descriptor.className || className;
    targets[className] = normalizeTarget(descriptor);
  };

  Fixit.prototype.instanceMethod = function (methodName, handler) {
    return register(this.target, methodName, false, handler);
  };

  Fixit.prototype.classMethod = function (methodName, handler) {
    return register(this.target, methodName, true, handler);
  };

  Object.defineProperty(Fixit, 'runtimeVersion', {
    value: '1.6.0',
    enumerable: true
  });

  function normalizeDelay(value, repeating) {
    var delay = Number(value);
    if (!isFinite(delay) || delay < 0) {
      delay = 0;
    }
    delay = Math.min(Math.floor(delay), 2147483647);
    return repeating && delay === 0 ? 1 : delay;
  }

  function allocateTimerId() {
    var start = nextTimerId;
    do {
      var id = nextTimerId;
      nextTimerId = nextTimerId >= 2147483647 ? 1 : nextTimerId + 1;
      if (!own(timers, id)) {
        return id;
      }
    } while (nextTimerId !== start);
    throw new Error('OhosPatch timer limit reached');
  }

  function scheduleTimer(callback, delay, repeating, args) {
    if (typeof callback !== 'function') {
      throw new TypeError('Timer callback must be a function');
    }
    var id = allocateTimerId();
    var normalizedDelay = normalizeDelay(delay, repeating);
    timers[id] = {
      callback: callback,
      args: args,
      repeating: repeating
    };
    if (!global.__ohospatch_scheduleTimer(id, normalizedDelay, repeating)) {
      delete timers[id];
      throw new Error('Unable to schedule OhosPatch timer');
    }
    return id;
  }

  function clearTimer(id) {
    var timerId = Number(id);
    if (!own(timers, timerId)) {
      return;
    }
    delete timers[timerId];
    global.__ohospatch_cancelTimer(timerId);
  }

  function clearAllTimers() {
    Object.keys(timers).forEach(function (id) {
      global.__ohospatch_cancelTimer(Number(id));
    });
    timers = Object.create(null);
  }

  function formatLogValue(value) {
    if (typeof value === 'string') {
      return value;
    }
    try {
      var json = JSON.stringify(value);
      return json === undefined ? String(value) : json;
    } catch (_) {
      return String(value);
    }
  }

  function log(level, args) {
    var text = Array.prototype.map.call(args, formatLogValue).join(' ');
    global.__ohospatch_hilog(level, text);
  }

  global.Fixit = Fixit;
  global.nil = null;
  global.Nil = null;
  global.nilToNull = function (value) {
    return value == null ? null : value;
  };
  global.nullToNil = function (value) {
    return value === null ? global.nil : value;
  };
  global.isNil = function (value) {
    return value === null || value === undefined;
  };
  global.require = Fixit.import;
  global.setTimeout = function (callback, delay) {
    return scheduleTimer(callback, delay, false, Array.prototype.slice.call(arguments, 2));
  };
  global.clearTimeout = clearTimer;
  global.setInterval = function (callback, delay) {
    return scheduleTimer(callback, delay, true, Array.prototype.slice.call(arguments, 2));
  };
  global.clearInterval = clearTimer;
  global.setImmediate = function (callback) {
    return scheduleTimer(callback, 0, false, Array.prototype.slice.call(arguments, 1));
  };
  global.clearImmediate = clearTimer;
  global.queueMicrotask = function (callback) {
    if (typeof callback !== 'function') {
      throw new TypeError('Microtask callback must be a function');
    }
    Promise.resolve().then(callback);
  };
  global.console = {
    debug: function () { log('debug', arguments); },
    log: function () { log('info', arguments); },
    info: function () { log('info', arguments); },
    warn: function () { log('warn', arguments); },
    error: function () { log('error', arguments); }
  };

  global.__ohospatch_specs = function () {
    return JSON.stringify(specs);
  };

  global.__ohospatch_uiSpecs = function () {
    return JSON.stringify(uiSpecs);
  };

  global.__ohospatch_clear = function () {
    clearAllTimers();
    registry.instance = Object.create(null);
    registry.klass = Object.create(null);
    registry.uiValues = Object.create(null);
    registry.uiAttrs = Object.create(null);
    registry.uiEvents = Object.create(null);
    specs = [];
    uiSpecs = [];
    uiRuleKeys = Object.create(null);
    nextUiRuleId = 1;
    targets = Object.create(null);
    importedProxyCache = Object.create(null);
  };

  global.__ohospatch_fireTimer = function (id) {
    var timer = timers[id];
    if (!timer) {
      return;
    }
    if (!timer.repeating) {
      delete timers[id];
    }
    timer.callback.apply(global, timer.args);
  };

  global.__ohospatch_callPatch = function (identity, methodName, isClassMethod, targetHandle, args) {
    var handler = bucket(isClassMethod)[key(identity, methodName)];
    if (!handler) {
      return { handled: false };
    }
    nativeProxyMetadata = new WeakMap();
    nativeProxyCache = Object.create(null);
    try {
      var target = makeNativeProxy(targetHandle, false, targetHandle);
      return {
        handled: true,
        result: encodePatchResult(handler.apply(target, args))
      };
    } finally {
      nativeProxyMetadata = new WeakMap();
      nativeProxyCache = Object.create(null);
    }
  };

  global.__ohospatch_callUiValue = function (ruleId, value) {
    var handler = registry.uiValues[ruleId];
    if (!handler) {
      return { handled: false };
    }
    return {
      handled: true,
      value: handler(value)
    };
  };

  global.__ohospatch_callUiEvent = function (ruleId, eventArgs, state, ownerHandle) {
    var handler = registry.uiEvents[ruleId];
    if (!handler) {
      return { handled: false };
    }
    if (!Array.isArray(eventArgs)) {
      eventArgs = [eventArgs || {}];
    }
    var statePatch = Object.create(null);
    var context = {
      state: state || {},
      setState: function (patch) {
        if (!patch || typeof patch !== 'object' || Array.isArray(patch)) {
          throw new TypeError('Component event state patch must be an object');
        }
        Object.keys(patch).forEach(function (name) {
          statePatch[validateUiName(name, 'Component state property')] = patch[name];
        });
      }
    };
    Object.defineProperty(context, UI_EVENT_CONTEXT_KEY, { value: true });
    var owner = typeof ownerHandle === 'number' ? makeNativeProxy(ownerHandle, false, ownerHandle) : undefined;
    try {
      var args = eventArgs.slice();
      args.push(context);
      var result = handler.apply(owner, args);
      return {
        handled: true,
        result: result,
        statePatch: statePatch
      };
    } finally {
      nativeProxyMetadata = new WeakMap();
      nativeProxyCache = Object.create(null);
    }
  };

  global.__ohospatch_callUiAttr = function (ruleId, ownerHandle) {
    var handler = registry.uiAttrs[ruleId];
    if (!handler) {
      return { handled: false };
    }
    var owner = typeof ownerHandle === 'number' ? makeNativeProxy(ownerHandle, false, ownerHandle) : undefined;
    try {
      return { handled: true, value: handler.call(owner) };
    } catch (err) {
      console.error('OhosPatch attribute handler failed: ' + (err && err.message ? err.message : err));
      return { handled: false };
    }
  };
})(typeof globalThis === 'undefined' ? this : globalThis);
