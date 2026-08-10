# OhosPatch

OhosPatch 是 [FIXiT](https://github.com/rickytan/FIXiT) 在 HarmonyOS/OpenHarmony 上的原型实现。宿主 APP 负责下载和验证 patch，再将完整 JavaScript 字符串或本地文件绝对路径交给 OhosPatch。OhosPatch 在独立 JSVM 中执行脚本，并通过 ArkTS 主 VM 的对象原型替换业务方法。业务类不需要继承基类、添加装饰器或调用补丁分发 API。

## 工程结构

```text
OhosPatch/
├── ohospatch/                 # 可复用 HAR 模块
│   ├── Index.ets              # HAR 对外 API
│   └── src/main/
│       ├── cpp/               # JSVM、NAPI、prototype hook
│       │   └── runtime/       # 内置 Fixit JS runtime
│       ├── ets/               # 字符串和本地文件执行 API
│       └── module.json5       # 无权限、无启动任务的 HAR 清单
├── entry/                     # Demo APP
│   └── src/main/ets/
│       ├── demo/              # 未侵入的业务类和验证场景
│       ├── patch/             # 宿主下载、验签接入点和启动策略
│       └── pages/             # 验证页面
└── patch-server/              # 运行时 patch 服务，不打入 HAP/HAR
```

`ohospatch` 可以独立构建为 `ohospatch.har`。`entry` 没有 Native 或补丁实现源码，只通过 `file:../ohospatch` 依赖接入 HAR。

## 实现原理

iOS FIXiT 同时依赖 JavaScriptCore 和 Objective-C runtime。JavaScriptCore 负责执行补丁，Objective-C runtime 负责动态替换方法。

HarmonyOS 中，`OH_JSVM_CreateVM` 创建的独立 JSVM 与 ArkTS 主 VM 不共享对象堆和原型链。因此，直接在 JSVM 中修改 `DemoViewModel.prototype` 无法影响 ArkTS 业务对象。OhosPatch 使用两层运行时协作：

1. 宿主将已验证的 JavaScript 字符串或本地绝对路径传给 HAR。
2. HAR 的 Native 模块创建独立 JSVM 并执行 JavaScript。
3. patch 注册目标类、模块、方法和 JS handler。
4. Native 使用 `napi_load_module_with_info` 从 ArkTS 主 VM 加载目标模块。
5. 实例方法替换 `constructor.prototype[methodName]`，静态方法替换 `constructor[methodName]`。
6. 原函数保存为 `napi_ref`，新函数使用 Native trampoline 转入 JSVM。
7. JS 中调用 `origin.apply(...)` 时，通过保存的 `napi_ref` 回调原 ArkTS 方法。
8. `clear()` 恢复原型上的原函数并释放引用。

普通 public 方法调用会经过对象属性和原型查找，因此已创建的业务实例也会在替换后进入 patch。

声明式组件使用 API 20 状态管理 V1 适配器。Native 在目标组件原型上包装编译产物的参数初始化、首次渲染和节点创建入口；参数和状态在原渲染前转换，节点 builder 执行后再写入属性并注册事件回调。公开 DSL 不暴露这些编译器生成的方法名。

## 内置 JS Runtime

`ohospatch/src/main/cpp/runtime/fixit.js` 定义 `Fixit` 构造函数和 patch 常用全局函数。CMake 在构建 HAR 时将该文件嵌入 `libohospatch.so`；JSVM 创建后先执行内置 runtime，再执行宿主传入的 patch，因此宿主和 patch 都不需要单独加载它。

内置 API：

- `Fixit.fix(target)`：创建目标类的 patch 对象。
- `Fixit.component(target)`：创建声明式组件 patch 对象。
- `component.param(name)` / `component.state(name)`：转换或替换组件参数与状态。
- `component.node({ type, occurrence })`：按 ArkUI 节点类型和同类型出现序号选择节点。
- `node.attr(name, ...args)` / `node.attrs({...})`：覆盖节点属性。
- `node.event(name, rule)`：替换节点事件并按需读取、更新组件状态。
- `Fixit.registerTarget(className, descriptor)`：注册类名到 HarmonyOS 模块描述符的映射，使后续可以使用 `Fixit.fix('ClassName')`。
- `instanceMethod(name, handler)` / `classMethod(name, handler)`：替换实例方法或静态方法，并返回原实现代理。
- `require(fullPath)`：解析目标类完整 OHM 源路径，生成 `Fixit.fix` 使用的类描述符。
- `nil` / `Nil`、`isNil`、`nilToNull`、`nullToNil`。
- `console.debug/log/info/warn/error`：输出到 HiLog 的 `OhosPatch` tag。
- `setTimeout` / `clearTimeout`、`setInterval` / `clearInterval`、`setImmediate` / `clearImmediate`。
- `queueMicrotask(callback)`：将回调加入 JSVM microtask 队列。

独立 JSVM 与 ArkTS 主 VM 不共享对象，因此 `require()` 返回的是类描述符，不是 ArkTS Constructor。安装 Hook 时，Native 根据描述符调用 `napi_load_module_with_info`，在主 ArkTS VM 中加载目标模块并取得导出的类。

Timer callback 和参数保存在 JSVM 内，Native 仅通过宿主 N-API 的 libuv event loop 调度 timer ID。`clear()`、下一次 `executeScript` 替换 patch 或 JSVM 重置时都会取消旧 timer，避免旧 patch 的异步任务继续执行。

Native 使用 `-fno-exceptions` 构建，不使用 C++ `throw/catch`。JSVM/NAPI 桥接失败会输出 `OhosPatch` error 级别 HiLog；patch 执行失败时回退原 ArkTS 方法，Hook 安装失败时回滚已安装的方法，`executeScript` 返回 `0`。参数校验、文件读取及业务原方法自身的异常仍保留在 ArkTS 层，其中原方法异常不会在 C++ 中捕获或转换。

## HAR 接入

Demo APP 的模块依赖：

```json5
{
  "dependencies": {
    "ohospatch": "file:../ohospatch"
  }
}
```

HAR 不声明 `ohos.permission.INTERNET`，不包含 HTTP 客户端、签名实现、patch URL、缓存或 AppStartup 任务。宿主根据自己的发布系统和启动策略完成这些工作。

执行完整 patch 字符串：

```ts
import { OhosPatch } from 'ohospatch';

const hookCount = OhosPatch.executeScript(verifiedPatchScript);
```

执行本地 patch 文件：

```ts
const hookCount = OhosPatch.executeFile(absolutePatchPath);
```

`executeFile` 只接受完整绝对路径，由 HAR 使用 `fileIo.readTextSync` 读取后执行。路径来源、文件权限、下载和签名验证仍由宿主负责。

Demo APP 在自己的 `DemoPatchStartupTask` 中通过 HTTP 下载脚本，并在交给 OhosPatch 前保留宿主验签位置。网络权限、patch URL 和 AppStartup 配置都位于 `entry`。业务类 `DemoViewModel` 和业务调用代码不引用 OhosPatch。

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

推荐使用完整 OHM 源路径加载其他模块中的目标类：

```js
var DemoViewModel = require(
  'com.rickytan.ohospatch/entry/src/main/ets/demo/DemoViewModel#DemoViewModel'
);
var fix = Fixit.fix(DemoViewModel);
```

完整路径格式为 `bundleName/moduleName/[packageName/]src/main/ets/File#ExportName`，也接受 `@bundle:` 前缀和 `.ets` / `.ts` 后缀。启用 `useNormalizedOHMUrl` 且 `oh-package.json5` 的 `name` 与 `moduleName` 不同时，需要提供 `packageName`；两者相同时可省略。`ExportName` 省略时默认使用文件名。以上示例自动解析为：

```text
modulePath = entry/src/main/ets/demo/DemoViewModel
moduleInfo = com.rickytan.ohospatch/entry
exportName = DemoViewModel
```

演示补丁位于 `patch-server/patch.js`，不会打入 HAR 或 HAP。

### 声明式组件 DSL

目标必须是业务模块导出的 API 20 状态管理 V1 自定义组件。业务组件本身不引用 OhosPatch，也不需要基类、装饰器或转发代码：

```js
var PatchablePanel = require(
  'com.rickytan.ohospatch/entry/src/main/ets/demo/PatchablePanel#PatchablePanel'
);
var panel = Fixit.component(PatchablePanel);

panel.param('message').replace('Patched component parameter');
panel.state('tapCount').transform(function (value) {
  return value === 0 ? 40 : value;
});

panel.node({ type: 'Button', occurrence: 0 })
  .attrs({ height: 52, backgroundColor: '#C44736' })
  .event('onClick', {
    mode: 'replace',
    capture: ['tapCount'],
    handler: function (_event, context) {
      context.setState({ tapCount: context.state.tapCount + 10 });
    }
  });
```

`occurrence` 从 `0` 开始，只在同一目标组件、同一节点类型内计数。属性参数必须可 JSON 序列化。事件首版只支持同步 `replace`；`capture` 最多读取 16 个组件属性，`context.setState()` 的对象会通过组件访问器写回 ArkTS 主 VM。patch handler 不存在或执行失败时，已安装的事件 trampoline 会回调原业务事件。

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

## GitHub Actions

`.github/workflows/harmonyos-build.yml` 提供两级 CI：

- push 和 pull request 在 GitHub-hosted Ubuntu runner 上执行 JS runtime 单元测试。
- 手动运行 `HarmonyOS CI` workflow 时，在自托管 macOS ARM64 runner 上构建 HAR 和未签名 HAP，并上传为保留 14 天的 artifact。
- 设置仓库变量 `HARMONYOS_CI_ENABLED=true` 后，`main` 分支每次 push 也会自动打包。pull request 不会执行自托管打包任务。

打包 runner 需要注册 `self-hosted`、`macOS`、`ARM64`、`harmonyos` 标签，GitHub Actions Runner 版本不低于 `2.327.1`，并预装 DevEco Studio、Command Line Tools 和 OpenHarmony API 20 SDK。默认从以下位置查找工具：

```text
/Applications/DevEco-Studio.app/Contents
$HOME/Library/OpenHarmony/Sdk
```

路径不同时，通过仓库变量 `DEVECO_STUDIO_HOME` 和 `OHOS_BASE_SDK_HOME` 覆盖。当前工程没有签名配置，因此 CI 产出的 HAP 仅用于编译验证；发布包仍需在受控环境注入证书与 Profile。

## Demo 验证

启动动态 patch 服务：

```bash
node patch-server/server.mjs
```

设备或模拟器通过 HDC 反向连接本机服务：

```bash
hdc rport tcp:8080 tcp:8080
```

安装并启动 Demo 后，宿主的 AppStartup 任务会下载 patch 字符串并调用 HAR，页面应显示越界访问、实例方法和静态方法均已修复。组件区域应显示 `Patched component parameter | taps=40` 和红色加高按钮；点击 `Component action` 后计数应变为 `50`，证明原来的 `+1` 回调已被替换。远程 patch 还会执行 `setTimeout`；等待 100 ms 后点击 `Run again`，实例方法结果应包含 `timer=fired`，HiLog 应出现 `OhosPatch setTimeout callback fired`。停止 patch 服务并重新冷启动应用时，页面应恢复为原始异常结果，从而证明 patch 不是本地打包资源。

## 当前边界

- prototype hook 不覆盖构造函数、实例字段形式的箭头函数、私有实现或不经过属性查找的调用点。
- ArkTS 与 JSVM 当前使用 JSON 数据桥，不能保留任意对象身份、循环引用、函数或 Native 对象。
- 声明式组件 DSL 首版只支持 API 20 状态管理 V1、业务模块导出的自定义组件、`type + occurrence` 节点选择器、JSON 属性参数和同步事件替换。
- 非导出的 `@Entry` 页面、状态管理 V2、层级/ID 选择器、资源与控制器类型、已挂载组件的主动刷新，以及 `before/after/around/origin` 事件模式尚未支持。
- 单个 runtime 最多同时存在 256 个 timer；`setInterval(..., 0)` 会按 1 ms 调度。
- 生产宿主必须在调用 HAR 前完成非对称签名校验、版本和设备匹配、灰度、缓存、回滚、超时与熔断。
- HAR 不决定下载方式和启动时机，宿主可以在 AppStartup、业务初始化或其他受控阶段调用。
