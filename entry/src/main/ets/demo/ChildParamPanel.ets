@Component
export struct ChildParamPanel {
  @Prop title: string = 'child';
  @Prop marker: string = 'marker';

  build() {
    Column() {
      Text(`${this.marker}:${this.title}`)
        .id(`child-param-${this.marker}`)
        .fontSize(13)
        .fontColor('#27313D')
        .width('100%')
        .padding(8)
        .backgroundColor('#FFFFFF')
        .borderRadius(8)
    }
    .width('100%')
  }
}

@Component
export struct ParentChildParamPanel {
  @State dynamicTitle: string = 'dynamic child';

  build() {
    Column({ space: 8 }) {
      ChildParamPanel({
        marker: 'first',
        title: 'static child'
      })
      ChildParamPanel({
        marker: 'second',
        title: this.dynamicTitle
      })
    }
    .width('100%')
    .padding(10)
    .backgroundColor('#EEF2F5')
    .borderRadius(8)
  }
}
