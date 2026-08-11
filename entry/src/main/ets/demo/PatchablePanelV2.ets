@ComponentV2
export struct PatchablePanelV2 {
  @Param message: string = 'Original V2 component parameter';
  @Local tapCount: number = 0;
  @Local statusText: string = 'V2 waiting for action';

  markPrimary(delta: number): void {
    this.tapCount += delta;
    this.statusText = `V2 event ran, taps=${this.tapCount}`;
  }

  build() {
    Column({ space: 10 }) {
      Text(this.message)
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor('#142033')
        .width('100%')

      Text(`tapCount=${this.tapCount}`)
        .fontSize(14)
        .fontColor('#27313D')
        .width('100%')
        .padding(10)
        .backgroundColor('#EEF2F5')

      Text(`status=${this.statusText}`)
        .fontSize(14)
        .fontColor('#27313D')
        .width('100%')

      Button('V2 primary action')
        .width('100%')
        .height(44)
        .backgroundColor('#59636E')
        .onClick(() => {
          this.markPrimary(1);
        })
    }
    .width('100%')
    .padding(14)
    .backgroundColor('#FFFFFF')
  }
}
