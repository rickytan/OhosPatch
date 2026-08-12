# OhosPatch

OhosPatch is a runtime JavaScript patching framework for HarmonyOS/OpenHarmony ArkTS apps.
It executes patch scripts inside an isolated JSVM and bridges to the host ArkTS VM through
Native N-API to patch class instance methods and declarative ArkUI components.

Project repository: https://github.com/rickytan/OhosPatch

## Install

```bash
ohpm install @rickytan/ohospatch
```

## Usage

```ts
import { OhosPatch } from '@rickytan/ohospatch';

OhosPatch.loadPatch('/data/storage/el2/base/files/patch.js');
OhosPatch.evaluatePatch('fix("@normalized:N&&&entry/src/main/ets/model/DemoViewModel&", "DemoViewModel").method("title", function (origin) { return "patched"; });');
OhosPatch.clear();
```

The host app is responsible for downloading, storing, and verifying patch scripts before
passing a local full path or a full script string to OhosPatch.
