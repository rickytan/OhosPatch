import { DemoResult, DemoScenario } from '../demo/DemoScenario';
import { PatchablePanel } from '../demo/PatchablePanel';

@Entry
@Component
struct Index {
  @State private title: string = 'OhosPatch';
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
    Column({ space: 18 }) {
      Text(this.title)
        .fontSize(24)
        .fontWeight(FontWeight.Bold)
        .fontColor('#18202A')
        .width('100%')

      Button('Run again')
        .width('100%')
        .height(44)
        .onClick(() => {
          this.runDemo();
        })

      PatchablePanel({ message: 'Original component parameter' })

      Column({ space: 10 }) {
        ForEach(this.rows, (row: string) => {
          Text(row)
            .fontSize(15)
            .fontColor('#27313D')
            .lineHeight(22)
            .width('100%')
            .padding(12)
            .backgroundColor('#FFFFFF')
            .borderRadius(8)
        }, (row: string) => row)
      }
      .width('100%')
    }
    .width('100%')
    .height('100%')
    .padding(20)
    .backgroundColor('#F7F8FA')
  }
}
