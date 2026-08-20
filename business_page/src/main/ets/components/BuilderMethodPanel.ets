@Component
export struct BuilderMethodPanel {
  @State statusText: string = 'Original @Builder state';

  @Builder
  contentBuilder() {
    Column({ space: 6 }) {
      Text('Builder method title')
        .fontSize(16)
        .fontColor('#27313D')
        .id('builder-method-title')

      Text(`builder method status=${this.statusText}`)
        .fontSize(14)
        .fontColor('#526070')
        .id('builder-method-status')

      Button('Builder method action')
        .height(42)
        .onClick(() => {
          this.statusText = 'Original @Builder event';
        })
        .id('builder-method-action')
    }
    .width('100%')
    .padding(10)
    .backgroundColor('#FFFFFF')
  }

  build() {
    Column({ space: 8 }) {
      Text('Outside Builder method')
        .fontSize(14)
        .id('builder-method-outside')

      this.contentBuilder()
    }
    .width('100%')
  }
}
