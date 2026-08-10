(function (global) {
  'use strict';

  var registry = {
    instance: Object.create(null),
    klass: Object.create(null)
  };
  var specs = [];
  var targets = Object.create(null);
  var timers = Object.create(null);
  var nextTimerId = 1;

  function own(object, property) {
    return Object.prototype.hasOwnProperty.call(object, property);
  }

  function key(targetKey, methodName) {
    return targetKey + '#' + methodName;
  }

  function bucket(isClassMethod) {
    return isClassMethod ? registry.klass : registry.instance;
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

  function parseRequiredTarget(fullPath) {
    if (typeof fullPath !== 'string' || fullPath.trim().length === 0) {
      throw new TypeError('require() expects a non-empty full module path');
    }

    var path = fullPath.trim();
    if (path.indexOf('@bundle:') === 0) {
      path = path.slice('@bundle:'.length);
    }
    if (path.indexOf('\\') !== -1 || path.charAt(0) === '/' || path.charAt(path.length - 1) === '/') {
      throw new Error('require() path must use bundleName/moduleName/[packageName/]src/main/ets/File format');
    }

    var hashIndex = path.indexOf('#');
    var exportName = '';
    if (hashIndex !== -1) {
      if (path.indexOf('#', hashIndex + 1) !== -1) {
        throw new Error('require() path can contain only one export separator (#)');
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
      throw new Error('require() path must use bundleName/moduleName/[packageName/]src/main/ets/File format');
    }
    for (var index = 0; index < parts.length; index += 1) {
      if (!parts[index]) {
        throw new Error('require() path contains an empty segment');
      }
    }

    var bundleName = parts[0];
    var moduleName = parts[1];
    var packageName = sourceOffset === 2 ? moduleName : parts[2];
    var fileName = parts[parts.length - 1];
    exportName = exportName || fileName;
    if (!/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(exportName)) {
      throw new Error('require() export name is invalid: ' + exportName);
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
      return global.__ohospatch_origin.apply(this, arguments);
    };
  }

  function Fixit(target, modulePath, exportName) {
    if (!(this instanceof Fixit)) {
      return new Fixit(target, modulePath, exportName);
    }
    this.target = normalizeTarget(target, modulePath, exportName);
  }

  Fixit.fix = function (target, modulePath, exportName) {
    return new Fixit(target, modulePath, exportName);
  };

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
    value: '1.2.0',
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
  global.require = parseRequiredTarget;
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

  global.__ohospatch_clear = function () {
    clearAllTimers();
    registry.instance = Object.create(null);
    registry.klass = Object.create(null);
    specs = [];
    targets = Object.create(null);
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

  global.__ohospatch_callPatch = function (identity, methodName, isClassMethod, target, args) {
    var handler = bucket(isClassMethod)[key(identity, methodName)];
    if (!handler) {
      return { handled: false };
    }
    return {
      handled: true,
      result: handler.apply(target, args),
      target: target
    };
  };
})(typeof globalThis === 'undefined' ? this : globalThis);
