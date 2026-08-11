import { OhosPatch } from '@rickytan/ohospatch';
import { loadPatch } from '../patch/PatchLoader';
import { DemoResult, DemoScenario } from '../demo/DemoScenario';
import { PatchablePanel } from '../demo/PatchablePanel';
import { PatchablePanelV2 } from '../demo/PatchablePanelV2';

@Entry
@Component
struct Index {
  @State private stack: NavPathStack = new NavPathStack();
  @State private patchState: string = 'Patch not loaded';
  @State private patchDetails: string = 'Use the local patch server, then load the remote script.';
  @State private loading: boolean = false;

  private async loadPatch(): Promise<void> {
    if (this.loading) {
      return;
    }
    this.loading = true;
    try {
      const hostContext = this.getUIContext().getHostContext();
      if (!hostContext) {
        throw new Error('Host context is unavailable');
      }
      const result = await loadPatch(hostContext, { forceNetwork: true });
      this.patchState = `Patch loaded: ${result.hookCount} hooks (${result.source})`;
      this.patchDetails = result.detail;
    } catch (err) {
      this.patchState = 'Patch load failed';
      this.patchDetails = (err as Error).message;
    } finally {
      this.loading = false;
    }
  }

  private clearPatch(): void {
    OhosPatch.clear();
    this.patchState = 'Patch cleared';
    this.patchDetails = 'Open the second screen again to compare original behavior.';
  }

  @Builder
  private DemoDestination(name: string) {
    if (name === 'demo') {
      NavDestination() {
        DemoPatchScreen()
      }
      .title('Patch Targets')
    }
  }

  build() {
    Navigation(this.stack) {
      Column({ space: 16 }) {
        Text('OhosPatch')
          .fontSize(28)
          .fontWeight(FontWeight.Bold)
          .fontColor('#142033')
          .width('100%')

        Text(this.patchState)
          .fontSize(18)
          .fontWeight(FontWeight.Medium)
          .fontColor('#1F6B46')
          .width('100%')

        Text(this.patchDetails)
          .fontSize(13)
          .fontColor('#526070')
          .lineHeight(19)
          .width('100%')

        Button(this.loading ? 'Loading patch...' : 'Load patch')
          .width('100%')
          .height(48)
          .enabled(!this.loading)
          .backgroundColor('#1F6B46')
          .onClick(() => {
            this.loadPatch();
          })

        Button('Clear patch')
          .width('100%')
          .height(48)
          .backgroundColor('#59636E')
          .onClick(() => {
            this.clearPatch();
          })

        Button('Open patch target screen')
          .width('100%')
          .height(48)
          .backgroundColor('#C44736')
          .onClick(() => {
            this.stack.pushPathByName('demo', null);
          })
      }
      .width('100%')
      .height('100%')
      .padding(20)
      .justifyContent(FlexAlign.Center)
      .backgroundColor('#F7F8FA')
    }
    .title('Patch Console')
    .mode(NavigationMode.Stack)
    .navDestination(this.DemoDestination)
  }
}

@Component
struct DemoPatchScreen {
  @State private title: string = 'Method hooks';
  @State private rows: Array<string> = [];

  aboutToAppear(): void {
    this.runDemo();
  }

  private runDemo(): void {
    const result: DemoResult = DemoScenario.run();
    this.title = result.title;
    this.rows = result.rows;
  }

  build() {
    Scroll() {
      Column({ space: 14 }) {
        PatchablePanel({
          message: 'Original component parameter',
          subtitle: 'State, attrs, events, this proxy, and original event callback'
        })

        PatchablePanelV2({
          message: 'Original V2 component parameter'
        })

        Button('Run method hook scenario')
          .width('100%')
          .height(44)
          .backgroundColor('#59636E')
          .onClick(() => {
            this.runDemo();
          })

        Text(this.title)
          .fontSize(20)
          .fontWeight(FontWeight.Bold)
          .fontColor('#142033')
          .width('100%')

        ForEach(this.rows, (row: string) => {
          Text(row)
            .fontSize(14)
            .fontColor('#27313D')
            .lineHeight(21)
            .width('100%')
            .padding(12)
            .backgroundColor('#FFFFFF')
            .borderRadius(8)
        }, (row: string) => row)
      }
      .width('100%')
      .padding(20)
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#F7F8FA')
  }
}
