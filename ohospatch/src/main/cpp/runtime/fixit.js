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
  var timers = Object.create(null);
  var nextTimerId = 1;
  var nativeProxyMetadata = new WeakMap();
  var nativeProxyCache = Object.create(null);
  var importedProxyMetadata = new WeakMap();
  var importedProxyCache = Object.create(null);

  var REMOTE_HANDLE_KEY = '__ohospatch_proxy_handle__';
  var IMPORT_HANDLE_KEY = '__ohospatch_import_handle__';
  var JSVM_FUNCTION_HANDLE_KEY = '__ohospatch_jsvm_function_handle__';
  var UNDEFINED_VALUE_KEY = '__ohospatch_proxy_undefined__';

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
      if (typeof value === 'function') {
        var functionHandle = global.__ohospatch_retainJsFunction(value);
        if (!functionHandle) {
          throw new TypeError('OhosPatch could not retain a function argument');
        }
        var functionReference = {};
        functionReference[JSVM_FUNCTION_HANDLE_KEY] = functionHandle;
        return functionReference;
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
      encoded = value.map((item) => {
        return encodeNativeWireValue(item, ancestors);
      });
    } else {
      encoded = {};
      Object.keys(value).forEach((property) => {
        encoded[property] = encodeNativeWireValue(value[property], ancestors);
      });
    }
    ancestors.pop();
    return encoded;
  }

  function encodeNativeWire(value) {
    return JSON.stringify(encodeNativeWireValue(value, []));
  }

  function encodeUiArgumentValue(value, ancestors) {
    if (typeof value === 'function') {
      var handle = global.__ohospatch_retainJsFunction(value);
      if (!handle) {
        throw new TypeError('OhosPatch could not retain a function argument');
      }
      var reference = {};
      reference[JSVM_FUNCTION_HANDLE_KEY] = handle;
      return reference;
    }
    if (value === undefined || typeof value === 'symbol') {
      throw new TypeError('OhosPatch UI argument contains an unsupported type');
    }
    if (value === null || typeof value === 'string' || typeof value === 'boolean') {
      return value;
    }
    if (typeof value === 'number') {
      return Number.isFinite(value) ? value : null;
    }
    if (typeof value !== 'object') {
      throw new TypeError('OhosPatch UI argument contains an unsupported type');
    }
    if (ancestors.indexOf(value) !== -1) {
      throw new TypeError('OhosPatch UI argument contains a circular reference');
    }

    ancestors.push(value);
    var encoded;
    if (Array.isArray(value)) {
      encoded = value.map((item) => {
        return encodeUiArgumentValue(item, ancestors);
      });
    } else {
      encoded = {};
      Object.keys(value).forEach((property) => {
        encoded[property] = encodeUiArgumentValue(value[property], ancestors);
      });
    }
    ancestors.pop();
    return encoded;
  }

  function copyUiArguments(value, label) {
    try {
      return JSON.parse(JSON.stringify(encodeUiArgumentValue(value, [])));
    } catch (err) {
      if (err instanceof TypeError) {
        throw new TypeError(label + ' must be JSON-serializable or contain function references');
      }
      throw err;
    }
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

  global.__ohospatch_makeNativeProxy = function (handle) {
    return makeNativeProxy(handle, false, handle);
  };

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
      moduleName: target.moduleName || ''
    };
  }

  function parseTargetPath(fullPath) {
    if (typeof fullPath !== 'string' || fullPath.trim().length === 0) {
      throw new TypeError('OhosPatch expects a non-empty full module path');
    }

    var path = fullPath.trim();
    var originalPath = path;
    if (path.indexOf('@bundle:') === 0) {
      path = path.slice('@bundle:'.length);
    }
    if (path.indexOf('\\') !== -1 || path.charAt(path.length - 1) === '/') {
      throw new Error('Invalid OhosPatch target path "' + originalPath + '": path must use bundleName/moduleName/[packagePath/]src/main/ets/File#Export, @package/src/main/ets/File#Export, or /src/main/ets/File#Export format');
    }

    var hashIndex = path.indexOf('#');
    var exportName = '';
    if (hashIndex !== -1) {
      if (path.indexOf('#', hashIndex + 1) !== -1) {
        throw new Error('Invalid OhosPatch target path "' + originalPath + '": path can contain only one export separator (#)');
      }
      exportName = path.slice(hashIndex + 1);
      path = path.slice(0, hashIndex);
    }
    if (/\.(ets|ts)$/.test(path)) {
      path = path.replace(/\.(ets|ts)$/, '');
    }

    var isHostModulePath = path.charAt(0) === '/';
    if (isHostModulePath) {
      path = path.slice(1);
    }

    var parts = path.split('/');
    for (var index = 0; index < parts.length; index += 1) {
      if (!parts[index]) {
        throw new Error('Invalid OhosPatch target path "' + originalPath + '": path contains an empty segment');
      }
    }

    var isPackagePath = path.charAt(0) === '@';
    var sourceSearchStart = isHostModulePath || isPackagePath ? 0 : 2;
    var sourceOffset = -1;
    for (var partIndex = sourceSearchStart; partIndex < parts.length - 2; partIndex += 1) {
      if (parts[partIndex] === 'src' &&
          parts[partIndex + 1] === 'main' &&
          parts[partIndex + 2] === 'ets') {
        sourceOffset = partIndex;
        break;
      }
    }
    if (sourceOffset === -1 || parts.length < sourceOffset + 4) {
      throw new Error('Invalid OhosPatch target path "' + originalPath + '": cannot find src/main/ets/File. Use bundleName/moduleName/[packagePath/]src/main/ets/File#Export, @package/src/main/ets/File#Export, or /src/main/ets/File#Export');
    }

    var fileName = parts[parts.length - 1];
    exportName = exportName || fileName;
    if (!/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(exportName)) {
      throw new Error('Invalid OhosPatch target path "' + originalPath + '": export name "' + exportName + '" is not a valid JavaScript identifier');
    }

    var bundleName = '';
    var moduleName = '';
    var modulePath = '';
    if (isHostModulePath) {
      if (sourceOffset !== 0) {
        throw new Error('Invalid OhosPatch target path "' + originalPath + '": host module shorthand must start with /src/main/ets');
      }
      modulePath = '/' + parts.slice(sourceOffset).join('/');
    } else if (isPackagePath) {
      modulePath = parts.join('/');
    } else {
      if (sourceOffset < 2) {
        throw new Error('Invalid OhosPatch target path "' + originalPath + '": full path must include bundleName and moduleName before src/main/ets');
      }
      if (parts[1].charAt(0) === '@') {
        throw new Error('Invalid OhosPatch target path "' + originalPath + '": full path must include bundleName/moduleName before package path');
      }
      bundleName = parts[0];
      moduleName = parts[1];
      modulePath = (sourceOffset === 2 ? parts[1] : parts.slice(2, sourceOffset).join('/')) +
        '/' + parts.slice(sourceOffset).join('/');
    }

    return {
      className: exportName === 'default' ? fileName : exportName,
      modulePath: modulePath,
      moduleInfo: bundleName && moduleName ? bundleName + '/' + moduleName : '',
      exportName: exportName,
      bundleName: bundleName,
      moduleName: moduleName
    };
  }

  function targetKey(target) {
    return target.moduleInfo + '|' + target.modulePath + '#' + target.exportName;
  }

  function normalizeTarget(target) {
    if (typeof target === 'string') {
      if (target.indexOf('/') !== -1 || target.indexOf('@bundle:') === 0) {
        var normalized = parseTargetPath(target);
        if (!normalized.className || !normalized.modulePath) {
          throw new Error('Fixit target "' + target + '" did not resolve to className and modulePath');
        }
        return normalized;
      }
      throw new Error('Fixit target "' + target + '" must be a full OHM path string. Example: com.example.app/entry/src/main/ets/Foo#Foo');
    }
    throw new TypeError('Fixit.fix requires a full OHM path string');
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
      return decodeNativeResponse(global.__ohospatch_eventOrigin(encodeNativeWire(args)), 0);
    };
  }

  function copyUiTarget(target) {
    var descriptor = copyTarget(target);
    descriptor.targetKey = targetKey(target);
    return descriptor;
  }

  function ComponentFix(target) {
    this.target = normalizeTarget(target);
  }

  function registerComponentValue(component, kind, propertyName, replacement) {
    var property = validateUiName(propertyName, 'Component property name');
    var handler;
    if (typeof replacement === 'function') {
      handler = replacement;
    } else {
      var copiedReplacement = copyJsonValue(replacement, 'Component replacement value');
      handler = function () {
        return copiedReplacement;
      };
    }
    if (typeof handler !== 'function') {
      throw new TypeError('Component value replacement must be a JSON value or function');
    }
    var target = component.target;
    var uniqueKey = targetKey(target) + '|' + kind + '|' + property;
    var rule = copyUiTarget(target);
    rule.kind = kind;
    rule.propertyName = property;
    rule.operation = 'transform';
    registerUiRule(uniqueKey, rule, registry.uiValues, handler);
    return component;
  }

  function normalizeNodeSelector(selector) {
    var normalized = typeof selector === 'string' ? { type: selector } : selector;
    if (!normalized || typeof normalized !== 'object') {
      throw new TypeError('Component node selector must be a type string or descriptor');
    }
    var componentTarget = null;
    var type;
    if (typeof normalized.type === 'string' &&
        (normalized.type.indexOf('/') !== -1 || normalized.type.indexOf('#') !== -1)) {
      componentTarget = normalizeTarget(normalized.type);
      type = targetKey(componentTarget);
    } else {
      type = validateUiName(normalized.type, 'Component node type');
    }
    if (normalized.where !== undefined) {
      if (componentTarget) {
        throw new TypeError('Component child selector does not support where');
      }
      if (normalized.occurrence !== undefined) {
        throw new TypeError('Component node selector cannot combine where and occurrence');
      }
      if (!normalized.where || typeof normalized.where !== 'object' || Array.isArray(normalized.where)) {
        throw new TypeError('Component node where must be a non-empty object');
      }
      var names = Object.keys(normalized.where).sort();
      if (names.length === 0) {
        throw new TypeError('Component node where must be a non-empty object');
      }
      var where = {};
      names.forEach((name) => {
        validateUiName(name, 'Component node where attribute name');
        where[name] = copyJsonValue(normalized.where[name], 'Component node where value');
      });
      return {
        type: type,
        selectorKey: JSON.stringify({ type: type, where: where }),
        where: where
      };
    }
    var occurrence = normalized.occurrence === undefined ? 0 : Number(normalized.occurrence);
    if (!Number.isInteger(occurrence) || occurrence < 0 || occurrence > 4294967295) {
      throw new TypeError('Component node occurrence must be a non-negative uint32 integer');
    }
    return {
      type: type,
      occurrence: occurrence,
      selectorKey: componentTarget
        ? JSON.stringify({ component: type, occurrence: occurrence })
        : JSON.stringify({ type: type, occurrence: occurrence }),
      componentTarget: componentTarget
    };
  }

  function copyNodeSelector(rule, selector) {
    rule.nodeType = selector.type;
    rule.selectorKey = selector.selectorKey;
    if (selector.where) {
      rule.where = selector.where;
    } else {
      rule.occurrence = selector.occurrence;
    }
  }

  function ComponentNodeFix(component, selector) {
    this.component = component;
    this.selector = normalizeNodeSelector(selector);
  }

  function ComponentBuilderNodeFix(component, selector) {
    this.component = component;
    this.childSelector = null;
    this.builderParamName = '';
    this.builderMethodName = '';
    this.selector = normalizeNodeSelector(selector);
    assertBuiltInNodeSelector(this.selector, 'builder node');
  }

  function ComponentBuilderMethodFix(component, builderMethodName) {
    this.component = component;
    this.builderMethodName = validateUiName(builderMethodName, 'Component Builder method name');
  }

  ComponentBuilderMethodFix.prototype.node = function (selector) {
    var fix = new ComponentBuilderNodeFix(this.component, selector);
    fix.builderMethodName = this.builderMethodName;
    return fix;
  };

  function assertBuiltInNodeSelector(selector, operation) {
    if (selector.componentTarget) {
      throw new TypeError('Component child selector does not support ' + operation);
    }
  }

  function assertChildNodeSelector(selector, operation) {
    if (!selector.componentTarget) {
      throw new TypeError('Built-in ArkUI node selector does not support ' + operation);
    }
  }

  ComponentNodeFix.prototype.attr = function (attributeName) {
    assertBuiltInNodeSelector(this.selector, 'attr');
    var name = validateUiName(attributeName, 'Component attribute name');
    var args = Array.prototype.slice.call(arguments, 1);
    if (args.length === 0) {
      throw new TypeError('Component attribute requires at least one argument');
    }

    var target = this.component.target;
    var selector = this.selector;
    var uniqueKey = targetKey(target) + '|node|' + selector.selectorKey + '|attr|' + name;
    var rule = copyUiTarget(target);
    rule.kind = 'attribute';
    copyNodeSelector(rule, selector);
    rule.attributeName = name;

    if (typeof args[0] === 'function') {
      if (args.length !== 1) {
        throw new TypeError('Component attribute handler does not accept extra arguments');
      }
      rule.attrHandler = true;
      registerUiRule(uniqueKey, rule, registry.uiAttrs, args[0]);
    } else {
      rule.attrHandler = false;
      rule.arguments = copyUiArguments(args, 'Component attribute arguments');
      registerUiRule(uniqueKey, rule, null, null);
    }
    return this;
  };

  ComponentNodeFix.prototype.attrs = function (attributes) {
    if (!attributes || typeof attributes !== 'object' || Array.isArray(attributes)) {
      throw new TypeError('Component attributes must be an object');
    }
    Object.keys(attributes).forEach((name) => {
      this.attr(name, attributes[name]);
    });
    return this;
  };

  ComponentNodeFix.prototype.event = function (eventName, handler) {
    assertBuiltInNodeSelector(this.selector, 'event');
    var name = validateUiName(eventName, 'Component event name');
    if (typeof handler !== 'function') {
      throw new TypeError('Component event handler must be a function');
    }

    var target = this.component.target;
    var selector = this.selector;
    var uniqueKey = targetKey(target) + '|node|' + selector.selectorKey + '|event|' + name;
    var rule = copyUiTarget(target);
    rule.kind = 'event';
    copyNodeSelector(rule, selector);
    rule.eventName = name;
    registerUiRule(uniqueKey, rule, registry.uiEvents, handler);
    return makeUiEventOrigin();
  };

  ComponentNodeFix.prototype.param = function (propertyName, replacement) {
    assertChildNodeSelector(this.selector, 'param');
    if (arguments.length !== 2) {
      throw new TypeError('Component child parameter requires a property name and replacement value or handler');
    }
    var property = validateUiName(propertyName, 'Component child parameter name');
    var handler;
    if (typeof replacement === 'function') {
      handler = replacement;
    } else {
      var copiedReplacement = copyJsonValue(replacement, 'Component child parameter replacement value');
      handler = function () {
        return copiedReplacement;
      };
    }
    var target = this.component.target;
    var selector = this.selector;
    var childTarget = selector.componentTarget;
    var uniqueKey = targetKey(target) + '|child|' + selector.selectorKey + '|param|' + property;
    var rule = copyUiTarget(target);
    rule.kind = 'childParam';
    copyNodeSelector(rule, selector);
    rule.propertyName = property;
    rule.childClassName = childTarget.className;
    rule.childModulePath = childTarget.modulePath;
    rule.childModuleInfo = childTarget.moduleInfo || '';
    rule.childExportName = childTarget.exportName;
    rule.childTargetKey = targetKey(childTarget);
    registerUiRule(uniqueKey, rule, registry.uiValues, handler);
    return this;
  };

  ComponentNodeFix.prototype.builder = function (builderParamName, selector) {
    if (arguments.length === 1) {
      selector = builderParamName;
      builderParamName = '';
    } else if (arguments.length !== 2) {
      throw new TypeError('Component builder requires a node selector and an optional BuilderParam name');
    }
    assertChildNodeSelector(this.selector, 'builder');
    var fix = new ComponentBuilderNodeFix(this.component, selector);
    fix.childSelector = this.selector;
    fix.builderParamName = builderParamName
      ? validateUiName(builderParamName, 'Component BuilderParam name')
      : '';
    return fix;
  };

  function builderScopeKey(builderFix) {
    if (builderFix.builderMethodName) {
      return '|builderMethod|' + builderFix.builderMethodName;
    }
    return '|builderParam|' + builderFix.childSelector.selectorKey + '|param|' + builderFix.builderParamName;
  }

  function copyBuilderScope(rule, builderFix) {
    if (builderFix.builderMethodName) {
      rule.builderMethodName = builderFix.builderMethodName;
      rule.selectorKey = 'builderMethod:' + builderFix.builderMethodName + '|' + rule.selectorKey;
      return;
    }
    var childTarget = builderFix.childSelector.componentTarget;
    rule.childClassName = childTarget.className;
    rule.childModulePath = childTarget.modulePath;
    rule.childModuleInfo = childTarget.moduleInfo || '';
    rule.childExportName = childTarget.exportName;
    rule.childTargetKey = targetKey(childTarget);
    rule.childOccurrence = builderFix.childSelector.occurrence;
    rule.builderParamName = builderFix.builderParamName;
  }

  function registerNodeCreate(componentFix, selector) {
    assertBuiltInNodeSelector(selector, 'create');
    var args = Array.prototype.slice.call(arguments, 2);
    var target = componentFix.target;
    var uniqueKey = targetKey(target) + '|node|' + selector.selectorKey + '|create';
    var rule = copyUiTarget(target);
    rule.kind = 'create';
    copyNodeSelector(rule, selector);
    rule.arguments = copyUiArguments(args, 'Component create arguments');
    registerUiRule(uniqueKey, rule, null, null);
    return componentFix;
  }

  function registerBuilderNodeCreate(builderFix) {
    var args = Array.prototype.slice.call(arguments, 1);
    var target = builderFix.component.target;
    var selector = builderFix.selector;
    var uniqueKey = targetKey(target) + builderScopeKey(builderFix) + '|node|' + selector.selectorKey + '|create';
    var rule = copyUiTarget(target);
    rule.kind = 'create';
    copyNodeSelector(rule, selector);
    copyBuilderScope(rule, builderFix);
    rule.arguments = copyUiArguments(args, 'Component builder create arguments');
    registerUiRule(uniqueKey, rule, null, null);
    return builderFix;
  }

  ComponentNodeFix.prototype.create = function () {
    registerNodeCreate.apply(null, [this.component, this.selector].concat(Array.prototype.slice.call(arguments)));
    return this;
  };

  ComponentBuilderNodeFix.prototype.attr = function (attributeName) {
    var name = validateUiName(attributeName, 'Component builder attribute name');
    var args = Array.prototype.slice.call(arguments, 1);
    if (args.length === 0) {
      throw new TypeError('Component builder attribute requires at least one argument');
    }

    var target = this.component.target;
    var selector = this.selector;
    var uniqueKey = targetKey(target) + builderScopeKey(this) + '|node|' + selector.selectorKey + '|attr|' + name;
    var rule = copyUiTarget(target);
    rule.kind = 'attribute';
    copyNodeSelector(rule, selector);
    copyBuilderScope(rule, this);
    rule.attributeName = name;

    if (typeof args[0] === 'function') {
      if (args.length !== 1) {
        throw new TypeError('Component builder attribute handler does not accept extra arguments');
      }
      rule.attrHandler = true;
      registerUiRule(uniqueKey, rule, registry.uiAttrs, args[0]);
    } else {
      rule.attrHandler = false;
      rule.arguments = copyUiArguments(args, 'Component builder attribute arguments');
      registerUiRule(uniqueKey, rule, null, null);
    }
    return this;
  };

  ComponentBuilderNodeFix.prototype.create = function () {
    registerBuilderNodeCreate.apply(null, [this].concat(Array.prototype.slice.call(arguments)));
    return this;
  };

  ComponentBuilderNodeFix.prototype.attrs = function (attributes) {
    if (!attributes || typeof attributes !== 'object' || Array.isArray(attributes)) {
      throw new TypeError('Component builder attributes must be an object');
    }
    Object.keys(attributes).forEach((name) => {
      this.attr(name, attributes[name]);
    });
    return this;
  };

  ComponentBuilderNodeFix.prototype.event = function (eventName, handler) {
    var name = validateUiName(eventName, 'Component builder event name');
    if (typeof handler !== 'function') {
      throw new TypeError('Component builder event handler must be a function');
    }

    var target = this.component.target;
    var selector = this.selector;
    var uniqueKey = targetKey(target) + builderScopeKey(this) + '|node|' + selector.selectorKey + '|event|' + name;
    var rule = copyUiTarget(target);
    rule.kind = 'event';
    copyNodeSelector(rule, selector);
    copyBuilderScope(rule, this);
    rule.eventName = name;
    registerUiRule(uniqueKey, rule, registry.uiEvents, handler);
    return makeUiEventOrigin();
  };

  ComponentFix.prototype.param = function (propertyName, replacement) {
    if (arguments.length !== 2) {
      throw new TypeError('Component parameter requires a property name and replacement value or handler');
    }
    return registerComponentValue(this, 'param', propertyName, replacement);
  };

  ComponentFix.prototype.state = function (propertyName, replacement) {
    if (arguments.length !== 2) {
      throw new TypeError('Component state requires a property name and replacement value or handler');
    }
    return registerComponentValue(this, 'state', propertyName, replacement);
  };

  ComponentFix.prototype.node = function (selector) {
    return new ComponentNodeFix(this, selector);
  };

  ComponentFix.prototype.builder = function (builderMethodName) {
    if (arguments.length !== 1) {
      throw new TypeError('Component builder requires exactly one @Builder method name');
    }
    return new ComponentBuilderMethodFix(this, builderMethodName);
  };

  function Fixit(target) {
    if (!(this instanceof Fixit)) {
      return new Fixit(target);
    }
    this.target = normalizeTarget(target);
  }

  Fixit.fix = function (target) {
    return new Fixit(target);
  };

  Fixit.component = function (target) {
    return new ComponentFix(target);
  };

  Fixit.import = importTarget;

  function encodeResourceOptions(options) {
    if (options === undefined || options === null) {
      return '';
    }
    return JSON.stringify(copyJsonValue(options, 'Resource options'));
  }

  function parseResourcePath(path) {
    if (typeof path !== 'string' || path.length === 0) {
      throw new TypeError('$r requires a non-empty resource path');
    }
    var parts = path.split('.');
    if (parts.length < 3 || parts[0] !== 'app') {
      throw new TypeError('$r resource path must use app.<type>.<name> format');
    }
    var type = parts[1];
    var name = parts.slice(2).join('.');
    if (!type || !name) {
      throw new TypeError('$r resource path must use app.<type>.<name> format');
    }
    return { type: type, name: name };
  }

  function resourceByType(kind, name, options) {
    if (typeof kind !== 'string' || kind.length === 0) {
      throw new TypeError('$r requires a non-empty resource kind');
    }
    if (typeof name !== 'string' || name.length === 0) {
      throw new TypeError('$r requires a non-empty resource name');
    }
    return global.__ohospatch_resource(kind, name, encodeResourceOptions(options));
  }

  function resource(path) {
    var parsed = parseResourcePath(path);
    var args = Array.prototype.slice.call(arguments, 1);
    if (parsed.type === 'string') {
      return resourceByType(parsed.type, parsed.name, args);
    }
    if (parsed.type === 'media' || parsed.type === 'image') {
      return resourceByType(parsed.type, parsed.name, args.length > 0 ? args[0] : undefined);
    }
    return resourceByType(parsed.type, parsed.name);
  }

  resource.string = function (name) {
    return resourceByType('string', name, Array.prototype.slice.call(arguments, 1));
  };
  resource.color = function (name) {
    return resourceByType('color', name);
  };
  resource.number = function (name) {
    return resourceByType('number', name);
  };
  resource.float = resource.number;
  resource.integer = resource.number;
  resource.int = resource.number;
  resource.stringArray = function (name) {
    return resourceByType('stringArray', name);
  };
  resource.media = function (name, density) {
    return resourceByType('media', name, density);
  };
  resource.image = resource.media;

  Fixit.resource = resource;
  Fixit.runtimeInfo = function () {
    return global.__ohospatch_runtimeInfo();
  };

  Fixit.prototype.instanceMethod = function (methodName, handler) {
    return register(this.target, methodName, false, handler);
  };

  Fixit.prototype.classMethod = function (methodName, handler) {
    return register(this.target, methodName, true, handler);
  };

  Object.defineProperty(Fixit, 'runtimeVersion', {
    value: '1.17.0',
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
    Object.keys(timers).forEach((id) => {
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
  global.$r = resource;
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

  global.__ohospatch_callUiValue = function (ruleId, value, ownerHandle) {
    var handler = registry.uiValues[ruleId];
    if (!handler) {
      return { handled: false };
    }
    var owner = typeof ownerHandle === 'number' ? makeNativeProxy(ownerHandle, false, ownerHandle) : undefined;
    try {
      return {
        handled: true,
        value: handler.call(owner, value)
      };
    } finally {
      nativeProxyMetadata = new WeakMap();
      nativeProxyCache = Object.create(null);
    }
  };

  global.__ohospatch_callUiEvent = function (ruleId, eventArgs, ownerHandle) {
    var handler = registry.uiEvents[ruleId];
    if (!handler) {
      return { handled: false };
    }
    if (!Array.isArray(eventArgs)) {
      eventArgs = [eventArgs || {}];
    }
    var owner = typeof ownerHandle === 'number' ? makeNativeProxy(ownerHandle, false, ownerHandle) : undefined;
    try {
      var result = handler.apply(owner, eventArgs);
      return {
        handled: true,
        result: result
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
