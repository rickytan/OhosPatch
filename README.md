# OhosPatch

OhosPatch 是 FIXiT 在 HarmonyOS/OpenHarmony 上的原型实现。补丁通过 HTTP 动态下发，在独立 JSVM 中执行，并通过 ArkTS 主 VM 的对象原型替换业务方法。业务类不需要继承基类、添加装饰器或调用补丁分发 API。

## 工程结构

```text
OhosPatch/
├── ohospatch/                 # 可复用 HAR 模块
│   ├── Index.ets              # HAR 对外 API
│   └── src/main/
│       ├── cpp/               # JSVM、NAPI、prototype hook
│       ├── ets/               # 下载 API 与 AppStartup 任务
│       ├── module.json5       # HAR 模块清单
│       └── resources/         # 启动任务和默认配置
├── entry/                     # Demo APP
│   └── src/main/ets/
│       ├── demo/              # 未侵入的业务类和验证场景
│       ├── entryability/      # 触发 HAR 启动任务
│       └── pages/             # 验证页面
└── patch-server/              # 运行时 patch 服务，不打入 HAP/HAR
```

`ohospatch` 可以独立构建为 `ohospatch.har`。`entry` 没有 Native 或补丁实现源码，只通过 `file:../ohospatch` 依赖接入 HAR。

## 实现原理

iOS FIXiT 同时依赖 JavaScriptCore 和 Objective-C runtime。JavaScriptCore 负责执行补丁，Objective-C runtime 负责动态替换方法。

HarmonyOS 中，`OH_JSVM_CreateVM` 创建的独立 JSVM 与 ArkTS 主 VM 不共享对象堆和原型链。因此，直接在 JSVM 中修改 `DemoViewModel.prototype` 无法影响 ArkTS 业务对象。OhosPatch 使用两层运行时协作：

1. HAR 的 Native 模块创建独立 JSVM 并执行远程 JavaScript。
2. patch 注册目标类、模块、方法和 JS handler。
3. Native 使用 `napi_load_module_with_info` 从 ArkTS 主 VM 加载目标模块。
4. 实例方法替换 `constructor.prototype[methodName]`，静态方法替换 `constructor[methodName]`。
5. 原函数保存为 `napi_ref`，新函数使用 Native trampoline 转入 JSVM。
6. JS 中调用 `origin.apply(...)` 时，通过保存的 `napi_ref` 回调原 ArkTS 方法。
7. `clear()` 恢复原型上的原函数并释放引用。

普通 public 方法调用会经过对象属性和原型查找，因此已创建的业务实例也会在替换后进入 patch。

## HAR 接入

Demo APP 的模块依赖：

```json5
{
  "dependencies": {
    "ohospatch": "file:../ohospatch"
  }
}
```

宿主还需要声明 `ohos.permission.INTERNET`，并提供 `ohospatch_patch_url` 字符串资源。

HarmonyOS 要求 HAR/HSP 内的 AppStartup 任务必须设置 `excludeFromAutoStart: true`，不能由 HAR 自动启动。OhosPatch 的任务实现和任务声明都在 HAR 中，Demo APP 只在启动阶段触发已注册任务：

```ts
startupManager.run(['OhosPatchStartupTask'], { timeoutMs: 6000 });
```

Demo 会等待任务完成后再加载页面，保证第一次业务调用发生在 patch 安装之后。业务类 `DemoViewModel` 和业务调用代码均不引用 OhosPatch。

## Patch 格式

跨模块加载需要提供目标 package 路径和 `bundleName/moduleName`：

```js
var fix = Fixit.fix({
  className: 'DemoViewModel',
  modulePath: 'entry/src/main/ets/demo/DemoViewModel',
  moduleInfo: 'com.rickytan.ohospatch/entry',
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

演示补丁位于 `patch-server/patch.js`，不会打入 HAR 或 HAP。

## 构建

构建独立 HAR：

```bash
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
  --mode module -p module=ohospatch assembleHar --no-daemon
```

产物：

```text
ohospatch/build/default/outputs/default/ohospatch.har
```

构建 Demo HAP：

```bash
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
  --mode module -p module=entry assembleHap --no-daemon
```

## Demo 验证

启动动态 patch 服务：

```bash
node patch-server/server.mjs
```

设备或模拟器通过 HDC 反向连接本机服务：

```bash
hdc rport tcp:8080 tcp:8080
```

安装并启动 Demo 后，页面应显示越界访问、实例方法和静态方法均已由远程 patch 修复。停止 patch 服务并重新冷启动应用时，页面应恢复为原始异常结果，从而证明 patch 不是本地打包资源。

## 当前边界

- prototype hook 不覆盖构造函数、实例字段形式的箭头函数、私有实现或不经过属性查找的调用点。
- ArkTS 与 JSVM 当前使用 JSON 数据桥，不能保留任意对象身份、循环引用、函数或 Native 对象。
- 生产环境必须增加非对称签名校验、版本和设备匹配、灰度、缓存、回滚、超时与熔断。
- 若要求宿主只添加依赖且连启动触发代码也不添加，需要额外提供 Hvigor 插件，在构建期注入宿主启动配置；HAR 本身不支持自动启动其任务。
