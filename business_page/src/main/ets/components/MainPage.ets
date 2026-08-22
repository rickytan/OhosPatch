import { DemoPatchScreen } from "./DemoPatch";

@Component
export struct MainPage {
  @State message: string = 'Hello World';

  build() {
    NavDestination() {
      Text('Nav Dest Content')
        .fontSize(30)
        .fontColor(Color.Blue)
      DemoPatchScreen()
    }
    .title('Patch Targets')
  }
}
