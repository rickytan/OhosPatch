@Component
export struct BuilderParamShell {
  @Prop title: string = 'BuilderParam shell';
  @BuilderParam contentBuilder: () => void;

  build() {
    Column({ space: 10 }) {
      Text(this.title)
        .fontSize(16)
        .fontWeight(FontWeight.Medium)
        .fontColor('#27313D')
        .width('100%')

      this.contentBuilder()
    }
    .width('100%')
    .padding(12)
    .backgroundColor('#FFFFFF')
    .borderRadius(8)
  }
}

@Component
export struct MultiBuilderParamShell {
  @Prop title: string = 'Multi BuilderParam shell';
  @BuilderParam headerBuilder: () => void;
  @BuilderParam contentBuilder: () => void;

  build() {
    Column({ space: 10 }) {
      Text(this.title)
        .fontSize(16)
        .fontWeight(FontWeight.Medium)
        .fontColor('#27313D')
        .width('100%')

      this.headerBuilder()
      Divider()
      this.contentBuilder()
    }
    .width('100%')
    .padding(12)
    .backgroundColor('#FFFFFF')
    .borderRadius(8)
  }
}

@Component
export struct BuilderParamRoot {
  @Prop message: string = 'Original builder-param root parameter';
  @State statusText: string = 'Original builder-param state';

  @Builder
  private SlotContent() {
    Text(`slot text=${this.statusText}`)
      .fontSize(14)
      .fontColor('#526070')
      .width('100%')
      .id('builder-slot-text')

    Button('Builder slot action')
      .width('100%')
      .height(44)
      .backgroundColor('#59636E')
      .id('builder-slot-button')
      .onClick(() => {
        this.statusText = 'Original builder slot clicked';
      })
  }

  @Builder
  private MultiHeader() {
    Text(`multi header text=${this.statusText}`)
      .fontSize(14)
      .fontColor('#526070')
      .width('100%')
      .id('multi-builder-header-text')

    Button('Multi header action')
      .width('100%')
      .height(44)
      .backgroundColor('#59636E')
      .id('multi-builder-header-button')
      .onClick(() => {
        this.statusText = 'Original multi header clicked';
      })
  }

  @Builder
  private MultiContent() {
    Text(`multi content text=${this.statusText}`)
      .fontSize(14)
      .fontColor('#526070')
      .width('100%')
      .id('multi-builder-content-text')

    Button('Multi content action')
      .width('100%')
      .height(44)
      .backgroundColor('#59636E')
      .id('multi-builder-content-button')
      .onClick(() => {
        this.statusText = 'Original multi content clicked';
      })
  }

  build() {
    Column({ space: 10 }) {
      Text(this.message)
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor('#142033')
        .width('100%')
        .id('builder-root-message')

      BuilderParamShell({ title: 'Custom BuilderParam shell' }) {
        this.SlotContent()
      }

      BuilderParamShell({ title: 'Inline BuilderParam shell' }) {
        Text(`inline slot text=${this.statusText}`)
          .fontSize(14)
          .fontColor('#526070')
          .width('100%')
          .id('builder-inline-slot-text')

        Button('Inline builder slot action')
          .width('100%')
          .height(44)
          .backgroundColor('#59636E')
          .id('builder-inline-slot-button')
          .onClick(() => {
            this.statusText = 'Original inline builder slot clicked';
          })
      }

      MultiBuilderParamShell({
        title: 'Explicit multi BuilderParam shell',
        headerBuilder: this.MultiHeader,
        contentBuilder: this.MultiContent
      })

      Text(`root status=${this.statusText}`)
        .fontSize(13)
        .fontColor('#526070')
        .width('100%')
        .id('builder-root-status')
    }
    .width('100%')
    .padding(14)
    .backgroundColor('#FFFFFF')
    .borderRadius(8)
  }
}
