import { DemoPatchScreen } from "./DemoPatch";

@Component
export struct MainPage {
  @State message: string = 'Hello World';
  @State private isExpanded: boolean = false

  @Builder
  contentBuilder() {
    Column({space: 8}) {
      Text('1').fontSize(20).backgroundColor(Color.Red)
      Text('2').fontSize(20).backgroundColor(Color.Yellow)
      if (this.isExpanded) {
        Text('100').fontSize(20)
      }
      Text('3').fontSize(20).backgroundColor(Color.Green)
    }
  }

  build() {
    NavDestination() {

      DemoPatchScreen()

      Button("Toggle").onClick(() => {
        this.isExpanded = !this.isExpanded
      })
      this.contentBuilder()
    }
    .title('Patch Targets')
  }
}
