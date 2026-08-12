import { DemoResult, DemoScenario } from "../ViewModel/DemoScenario";
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

        ParentChildParamPanel()

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

export {
  DemoPatchScreen
}