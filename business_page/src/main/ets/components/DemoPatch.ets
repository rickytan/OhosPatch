import { DemoResult, DemoScenario } from "../ViewModel/DemoScenario";
import { BuilderParamRoot } from "./BuilderParamPanel";
import { BuilderMethodPanel } from "./BuilderMethodPanel";
import { ParentChildParamPanel } from "./ChildParamPanel";
import { PatchablePanel } from "./PatchablePanel";
import { PatchablePanelV2 } from "./PatchablePanelV2";

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

  @Builder
  private ComponentTab() {
    Column({ space: 14 }) {
      Text('abcd')
        .fontSize(12)
        .fontColor('#59636E')
        .width('100%')

      PatchablePanel({
        message: 'Original component parameter',
        subtitle: 'State, attrs, events, this proxy, and original event callback'
      })
    }
    .width('100%')
    .padding(20)
    .id('patch-tab-components')
  }

  @Builder
  private V2Tab() {
    Column({ space: 14 }) {
      PatchablePanelV2({
        message: 'Original V2 component parameter'
      })

      ParentChildParamPanel()
    }
    .width('100%')
    .padding(20)
    .id('patch-tab-v2')
  }

  @Builder
  private BuilderParamTab() {
    Scroll() {
      Column({ space: 14 }) {
        BuilderParamRoot({
          message: 'Original builder-param root parameter'
        })
      }
      .width('100%')
      .padding(20)
      .id('patch-tab-builder-param')
    }
  }

  @Builder
  private BuilderMethodTab() {
    Column({ space: 14 }) {
      BuilderMethodPanel()
    }
    .width('100%')
    .padding(20)
    .id('patch-tab-builder-method')
  }

  @Builder
  private MethodTab() {
    Column({ space: 14 }) {
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
    .id('patch-tab-method')
  }

  build() {
    Column() {
      Tabs({ barPosition: BarPosition.Start }) {
        TabContent() {
          this.ComponentTab()
        }
        .align(Alignment.Top)
        .tabBar('Components')

        TabContent() {
          this.V2Tab()
        }
        .align(Alignment.Top)
        .tabBar('V2')

        TabContent() {
          this.BuilderParamTab()
        }
        .align(Alignment.Top)
        .tabBar('BuilderParam')

        TabContent() {
          this.BuilderMethodTab()
        }
        .align(Alignment.Top)
        .tabBar('BuilderMethod')

        TabContent() {
          this.MethodTab()
        }
        .align(Alignment.Top)
        .tabBar('Methods')
      }
      .width('100%')
      .height('100%')
      .barHeight(44)
      .barMode(BarMode.Scrollable)
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#F7F8FA')
    .id('patch-target-tabs')
  }
}

export {
  DemoPatchScreen
}
