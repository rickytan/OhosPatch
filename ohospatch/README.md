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

OhosPatch.init(context);

const hookCountFromFile: number = OhosPatch.executeFile('/data/storage/el2/base/files/patch.js');
const hookCountFromScript: number = OhosPatch.executeScript(`
  var fix = Fixit.fix('com.example.app/entry/src/main/ets/model/DemoViewModel#DemoViewModel');
  fix.instanceMethod('title', function () {
    return $r('app.string.patched_title');
  });
`);
OhosPatch.clear();
```

The host app is responsible for downloading, storing, and verifying patch scripts before
passing a local full path or a full script string to OhosPatch.

Call `OhosPatch.init(context)` once during host startup before scripts that use `$r` or
`Fixit.runtimeInfo()`.
