(function (global) {
  'use strict';

  var registry = {
    instance: Object.create(null),
    klass: Object.create(null)
  };
  var specs = [];
  var targets = Object.create(null);

  function own(object, property) {
    return Object.prototype.hasOwnProperty.call(object, property);
  }

  function key(className, methodName) {
    return className + '#' + methodName;
  }

  function bucket(isClassMethod) {
    return isClassMethod ? registry.klass : registry.instance;
  }

  function copyTarget(target) {
    return {
      className: target.className,
      modulePath: target.modulePath,
      moduleInfo: target.moduleInfo || '',
      exportName: target.exportName || target.className
    };
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
    var methodKey = key(target.className, methodName);
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
    value: '1.0.0',
    enumerable: true
  });

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
    global.__ohospatch_log(level, text);
  }

  global.Fixit = Fixit;
  global.nil = null;
  global.Nil = null;
  global.YES = true;
  global.NO = false;
  global.nilToNull = function (value) {
    return value == null ? null : value;
  };
  global.nullToNil = function (value) {
    return value === null ? global.nil : value;
  };
  global.isNil = function (value) {
    return value === null || value === undefined;
  };
  global.CGRectMake = function (x, y, width, height) {
    return { x: x, y: y, width: width, height: height };
  };
  global.CGPointMake = function (x, y) {
    return { x: x, y: y };
  };
  global.CGSizeMake = function (width, height) {
    return { width: width, height: height };
  };
  global.CGSize = global.CGSizeMake;
  global.NSMakeRange = function (location, length) {
    return { location: location, length: length };
  };
  global.require = function () {
    throw new Error(
      'require() cannot expose ArkTS objects inside an isolated JSVM; use a Fixit target descriptor instead'
    );
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
    registry.instance = Object.create(null);
    registry.klass = Object.create(null);
    specs = [];
    targets = Object.create(null);
  };

  global.__ohospatch_callPatch = function (className, methodName, isClassMethod, target, args) {
    var handler = bucket(isClassMethod)[key(className, methodName)];
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
