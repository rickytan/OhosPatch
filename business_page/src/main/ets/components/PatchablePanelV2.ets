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
        .id('v2-message-text')

      Text(`tapCount=${this.tapCount}`)
        .fontSize(14)
        .fontColor('#27313D')
        .width('100%')
        .padding(10)
        .backgroundColor('#EEF2F5')
        .id('v2-state-text')

      Text(`status=${this.statusText}`)
        .fontSize(14)
        .fontColor('#27313D')
        .width('100%')
        .id('v2-status-text')

      Button('V2 primary action')
        .width('100%')
        .height(44)
        .backgroundColor('#59636E')
        .onClick(() => {
          this.markPrimary(1);
        })
        .id('v2-primary-action')

      Button('V2 secondary action')
        .width('100%')
        .height(44)
        .backgroundColor(this.tapCount >= 0 ? '#7B5E2E' : '#59636E')
        .onClick(() => {
          this.markPrimary(2);
        })
        .id('v2-secondary-action')
    }
    .width('100%')
    .padding(14)
    .backgroundColor('#FFFFFF')
  }
}
