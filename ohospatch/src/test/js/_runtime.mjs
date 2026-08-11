// Shared mock infrastructure for the JS test suite.
//
// Extracted from fixit.test.mjs so that behavior tests and perf benchmarks can
// reuse the exact same createRuntime() mock (the JSVM<->Native bridge shim)
// without any test file importing another test file - which would make node's
// test runner execute the imported file's tests twice.

import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

const runtimeUrl = new URL('../../main/cpp/runtime/fixit.js', import.meta.url);
const runtimeSource = await readFile(runtimeUrl, 'utf8');

export function createRuntime() {
  const logs = [];
  const origins = [];
  const eventOrigins = [];
  const scheduledTimers = [];
  const cancelledTimers = [];
  const handles = [{}];
  const importedHandles = [];
  const importedClasses = new Map();

  function retain(value) {
    const existing = handles.indexOf(value);
    if (existing !== -1) {
      return existing;
    }
    handles.push(value);
    return handles.length - 1;
  }

  function response(value) {
    if (value === undefined) {
      return ['undefined'];
    }
    if ((typeof value === 'object' && value !== null) || typeof value === 'function') {
      return [typeof value === 'function' ? 'function' : 'object', retain(value)];
    }
    return ['value', value];
  }

  function retainImported(value) {
    const existing = importedHandles.indexOf(value);
    if (existing !== -1) {
      return existing;
    }
    importedHandles.push(value);
    return importedHandles.length - 1;
  }

  function importedResponse(value) {
    if (value === undefined) {
      return ['undefined'];
    }
    if ((typeof value === 'object' && value !== null) || typeof value === 'function') {
      return [typeof value === 'function' ? 'function' : 'object', retainImported(value)];
    }
    return ['value', value];
  }

  function decodeWire(value) {
    if (!value || typeof value !== 'object') {
      return value;
    }
    if (Object.hasOwn(value, '__ohospatch_proxy_handle__')) {
      return handles[value.__ohospatch_proxy_handle__];
    }
    if (Object.hasOwn(value, '__ohospatch_import_handle__')) {
      return importedHandles[value.__ohospatch_import_handle__];
    }
    if (Object.hasOwn(value, '__ohospatch_proxy_undefined__')) {
      return undefined;
    }
    Object.keys(value).forEach((key) => {
      value[key] = decodeWire(value[key]);
    });
    return value;
  }

  const context = vm.createContext({
    __ohospatch_hilog(level, message) {
      logs.push({ level, message });
    },
    __ohospatch_origin(wire) {
      const args = decodeWire(JSON.parse(wire));
      origins.push({ receiver: handles[0], args });
      return response('origin-result');
    },
    __ohospatch_eventOrigin(wire) {
      const args = decodeWire(JSON.parse(wire));
      eventOrigins.push({ receiver: handles[0], args });
      return response('event-origin-result');
    },
    __ohospatch_proxyGet(handle, property) {
      return response(handles[handle][property]);
    },
    __ohospatch_proxySet(handle, property, wire) {
      handles[handle][property] = decodeWire(JSON.parse(wire));
      return ['ok'];
    },
    __ohospatch_proxyCall(functionHandle, receiverHandle, wire) {
      const args = decodeWire(JSON.parse(wire));
      return response(handles[functionHandle].apply(handles[receiverHandle], args));
    },
    __ohospatch_import(targetJson) {
      const target = JSON.parse(targetJson);
      const imported = importedClasses.get(
        `${target.moduleInfo}|${target.modulePath}#${target.exportName}`
      );
      return imported === undefined
        ? ['error', `Missing import ${target.exportName}`]
        : importedResponse(imported);
    },
    __ohospatch_importGet(handle, property) {
      return importedResponse(importedHandles[handle][property]);
    },
    __ohospatch_importSet(handle, property, wire) {
      importedHandles[handle][property] = decodeWire(JSON.parse(wire));
      return ['ok'];
    },
    __ohospatch_importCall(functionHandle, receiverHandle, wire) {
      const args = decodeWire(JSON.parse(wire));
      return importedResponse(importedHandles[functionHandle].apply(importedHandles[receiverHandle], args));
    },
    __ohospatch_importConstruct(constructorHandle, wire) {
      const args = decodeWire(JSON.parse(wire));
      return importedResponse(Reflect.construct(importedHandles[constructorHandle], args));
    },
    __ohospatch_scheduleTimer(id, delay, repeating) {
      scheduledTimers.push({ id, delay, repeating });
      return true;
    },
    __ohospatch_cancelTimer(id) {
      cancelledTimers.push(id);
      return true;
    }
  });
  vm.runInContext(runtimeSource, context, { filename: 'fixit.js' });
  return {
    context,
    logs,
    origins,
    eventOrigins,
    scheduledTimers,
    cancelledTimers,
    registerImport(fullPath, value) {
      const target = context.Fixit.fix(fullPath).target;
      importedClasses.set(`${target.moduleInfo}|${target.modulePath}#${target.exportName}`, value);
    },
    setProxyRoot(value) {
      handles.length = 1;
      handles[0] = value;
    }
  };
}

// Behavior-test helpers. Tests never assert on spec field structure directly; these
// helpers locate a registered rule by its public identity (target + method name, or
// component selector) so test bodies stay focused on observable behavior.
export function findSpec(context, fix, methodName, isClassMethod) {
  const target = fix.target;
  return JSON.parse(context.__ohospatch_specs()).find((spec) =>
    spec.modulePath === target.modulePath &&
    spec.moduleInfo === target.moduleInfo &&
    spec.exportName === target.exportName &&
    spec.methodName === methodName &&
    !!spec.classMethod === !!isClassMethod
  );
}

export function invoke(context, fix, methodName, isClassMethod, args) {
  const spec = findSpec(context, fix, methodName, isClassMethod);
  if (!spec) {
    throw new Error(`No patch registered for ${methodName}`);
  }
  return context.__ohospatch_callPatch(spec.targetKey, methodName, isClassMethod, 0, args);
}

export function identityOf(context, fix, methodName, isClassMethod) {
  const spec = findSpec(context, fix, methodName, isClassMethod);
  if (!spec) {
    throw new Error(`No patch registered for ${methodName}`);
  }
  return spec.targetKey;
}

export function findUiRule(context, component, predicate) {
  const target = component.target;
  return JSON.parse(context.__ohospatch_uiSpecs()).find((spec) =>
    spec.modulePath === target.modulePath &&
    spec.moduleInfo === target.moduleInfo &&
    spec.exportName === target.exportName &&
    predicate(spec)
  );
}

export function valueRuleId(context, component, kind, propertyName) {
  const rule = findUiRule(context, component, (spec) =>
    spec.kind === kind && spec.propertyName === propertyName);
  if (!rule) {
    throw new Error(`No ${kind} rule for ${propertyName}`);
  }
  return rule.ruleId;
}

export function attrRuleId(context, component, type, occurrence, attributeName) {
  const rule = findUiRule(context, component, (spec) =>
    spec.kind === 'attribute' && spec.nodeType === type &&
    spec.occurrence === occurrence && spec.attributeName === attributeName);
  if (!rule) {
    throw new Error(`No attribute rule for ${type}.${attributeName}`);
  }
  return rule.ruleId;
}

export function eventRuleId(context, component, type, occurrence, eventName) {
  const rule = findUiRule(context, component, (spec) =>
    spec.kind === 'event' && spec.nodeType === type &&
    spec.occurrence === occurrence && spec.eventName === eventName);
  if (!rule) {
    throw new Error(`No event rule for ${type}.${eventName}`);
  }
  return rule.ruleId;
}

export function plain(value) {
  return JSON.parse(JSON.stringify(value));
}

export const PANEL_PATH = 'com.example.app/entry/src/main/ets/components/DemoPanel#DemoPanel';
