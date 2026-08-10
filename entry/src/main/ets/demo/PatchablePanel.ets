@Component
export struct PatchablePanel {
  @Prop message: string = 'Original component parameter';
  @State tapCount: number = 0;

  build() {
    Column({ space: 10 }) {
      Text(`${this.message} | taps=${this.tapCount}`)
        .fontSize(16)
        .fontColor('#18202A')
        .width('100%')

      Button('Component action')
        .width('100%')
        .height(44)
        .backgroundColor('#59636E')
        .onClick(() => {
          this.tapCount += 1;
        })
    }
    .width('100%')
    .padding(14)
    .backgroundColor('#FFFFFF')
    .borderRadius(8)
  }
}
