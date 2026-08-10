# OhosPatch

OhosPatch 是 FIXiT 在 HarmonyOS/OpenHarmony 上的原型实现。业务类不需要继承基类、添加装饰器或在方法中调用分发 API；补丁由 AppStartup 在运行时通过 HTTP 拉取，在独立 JSVM 中执行。

## FIXiT 原理

iOS 版 FIXiT 同时依赖两类能力：

1. JavaScriptCore 执行 JavaScript 补丁。
2. Objective-C runtime 将原 selector 的 IMP 替换成消息转发入口，并通过 `NSInvocation` 完成参数、返回值和原方法调用。

JavaScriptCore 只是补丁语言运行时，真正实现业务无感的是 Objective-C 的动态方法替换。

## 为什么不能只改 JSVM 原型

`OH_JSVM_CreateVM` 创建的是独立 JavaScript VM。ArkTS 业务实例和 JSVM 对象不共享堆、构造器或原型链，因此在 JSVM 中执行：

```js
DemoViewModel.prototype.crashIt = patch;
```

只能影响 JSVM 内的 `DemoViewModel`，无法影响 ArkTS 已加载的业务类。

可行做法是从 Native 模块当前的 `napi_env` 操作 ArkTS 主 VM：

1. 使用 `napi_load_module` 加载目标 ArkTS 模块并取得模块 namespace。
2. 从 namespace 取得导出的类构造器。
3. 实例方法替换 `constructor.prototype[methodName]`，静态方法替换 `constructor[methodName]`。
4. 原函数保存为 `napi_ref`，新函数是 Native trampoline。
5. trampoline 将调用转入独立 JSVM；补丁调用 `origin.apply(...)` 时，再通过保存的 `napi_ref` 调回原 ArkTS 方法。
6. `clear()` 恢复原型上的原函数并释放引用。

当前 API 20 编译产物中，普通方法调用使用 `ldobjbyname + callthis`，会经过对象属性和原型查找，因此这种替换对普通 public 实例方法和静态方法有效。

## 补丁格式

ArkTS 没有 Objective-C runtime 的全局 Class registry，因此补丁必须提供模块路径和导出名：

```js
var fix = Fixit.fix({
  className: 'DemoViewModel',
  modulePath: 'ets/demo/DemoViewModel',
  exportName: 'DemoViewModel'
});

var origin = fix.instanceMethod('locationOf', function (locations, index, fallback) {
  if (index < 0 || index >= locations.length) {
    this.buttonTitle = 'out of bounds';
    return fallback;
  }
  return origin.apply(this, arguments);
});

fix.classMethod('crash', function () {
  return 'fixed';
});
```

演示补丁位于 `patch-server/patch.js`，它在服务端目录，不会打进 HAP。

## 运行链路

```text
AppStartup
  -> HTTP 下载 patch.js
  -> Native 创建/进入独立 JSVM
  -> JS 注册 patch 与目标模块信息
  -> napi_load_module 获取 ArkTS 真实构造器
  -> 替换 prototype/constructor 方法为 trampoline
  -> 业务代码按原方式调用，自动进入 JSVM patch
```

`entry/src/main/ets/demo/DemoViewModel.ets` 是普通业务类，没有 OhosPatch import 或分发代码。`entry/src/main/resources/base/profile/ohospatch_startup.json` 负责启动时拉取补丁，地址来自资源 `ohospatch_patch_url`。

## 本地演示

启动补丁服务：

```bash
cd OhosPatch
node patch-server/server.mjs
```

真机通过 USB 调试时，将设备的 `127.0.0.1:8080` 反向到开发机：

```bash
hdc rport tcp:8080 tcp:8080
```

构建 HAP：

```bash
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
  --mode module -p module=entry assembleHap --no-daemon
```

## 接入边界

- 业务方法不需要修改，但宿主仍必须加载 SDK 并注册启动任务。本示例由 entry 的 AppStartup 配置完成。
- 普通 HAR 的启动任务按当前构建 schema 不能自动启动；若要做到接入方只添加依赖，需要再提供 Hvigor 插件，在构建期向宿主合并 AppStartup 配置。
- prototype Hook 不覆盖构造函数、实例字段形式的箭头函数、被编译器直接调用的私有实现，以及不经过属性查找的调用点。
- ArkTS 与 JSVM 当前使用 JSON 数据桥，不能保留任意对象身份、循环引用、函数和 Native 对象。完整实现需要对象句柄表和 JSVM Proxy bridge。
- 远程代码执行在生产环境必须加入非对称签名校验、版本/设备/应用匹配、灰度、超时、缓存、回滚和熔断。本示例只验证运行机制，不能直接作为生产热修复系统发布。

## 关键文件

- `entry/src/main/cpp/ohospatch.cpp`：JSVM、ArkTS prototype Hook、trampoline 和 origin 调用。
- `entry/src/main/ets/ohospatch/OhosPatch.ets`：HTTP 拉取及 Native API 封装。
- `entry/src/main/ets/ohospatch/OhosPatchStartupTask.ets`：无业务代码调用的启动安装入口。
- `entry/src/main/ets/demo/DemoViewModel.ets`：零侵入业务类。
- `patch-server/patch.js`：运行时下发的 FIXiT 风格补丁。
