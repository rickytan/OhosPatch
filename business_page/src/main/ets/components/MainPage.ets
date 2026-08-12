import { DemoPatchScreen } from "./DemoPatch";

@Component
export struct MainPage {
  @State message: string = 'Hello World';

  build() {
    NavDestination() {
      DemoPatchScreen()
    }
    .title('Patch Targets')
  }
}
