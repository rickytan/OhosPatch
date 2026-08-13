@Component
export struct PatchablePanel {
  @Prop message: string = 'Original component parameter';
  @Prop subtitle: string = 'Original subtitle';
  @State tapCount: number = 0;
  @State statusText: string = 'Waiting for action';
  @State secondaryCount: number = 1;
  @State switchOn: boolean = false;
  @State sliderValue: number = 20;
  @State tagText: string = 'original tag';
  unsafeCallback?: () => string;

  markPrimary(delta: number): void {
    this.tapCount += delta;
    this.statusText = `Primary event ran, taps=${this.tapCount}`;
  }

  markSecondary(label: string): string {
    this.secondaryCount += 1;
    this.tagText = `${label}:${this.secondaryCount}`;
    return this.tagText;
  }

  build() {
    Column({ space: 12 }) {
      Text(this.message)
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor('#142033')
        .width('100%')

      Text(this.subtitle)
        .fontSize(14)
        .fontColor('#526070')
        .width('100%')

      Text(`tapCount=${this.tapCount}, secondary=${this.secondaryCount}`)
        .fontSize(14)
        .fontColor('#27313D')
        .width('100%')
        .padding(10)
        .backgroundColor('#EEF2F5')
        .borderRadius(8)
        .id('v1-state-text')

      Text(`status=${this.statusText}`)
        .fontSize(14)
        .fontColor('#27313D')
        .width('100%')
        .padding(10)
        .backgroundColor('#FFFFFF')
        .borderRadius(8)
        .id('v1-status-text')

      Row({ space: 10 }) {
        Button('Primary action')
          .layoutWeight(1)
          .height(44)
          .backgroundColor('#59636E')
          .onClick(() => {
            this.markPrimary(1);
          })

        Button('Secondary action')
          .layoutWeight(1)
          .height(44)
          .backgroundColor('#7B5E2E')
          .onClick(() => {
            this.markSecondary('original');
          })
      }
      .width('100%')

      Button('Unsafe onClick crash')
        .width('100%')
        .height(44)
        .backgroundColor('#9B2C2C')
        .onClick(() => {
          this.tagText = (this.unsafeCallback as () => string)();
        })

      Row({ space: 10 }) {
        Toggle({ type: ToggleType.Switch, isOn: this.switchOn })
          .onChange((isOn: boolean) => {
            this.switchOn = isOn;
            this.statusText = `Switch changed to ${isOn}`;
          })

        Text(this.switchOn ? 'Switch is on' : 'Switch is off')
          .fontSize(14)
          .fontColor('#27313D')
          .layoutWeight(1)
      }
      .width('100%')
      .padding(10)
      .backgroundColor('#FFFFFF')
      .borderRadius(8)

      Slider({ value: this.sliderValue, min: 0, max: 100, step: 5 })
        .width('100%')
        .onChange((value: number, mode: SliderChangeMode) => {
          this.sliderValue = value;
          this.statusText = `Slider changed to ${value}, mode=${mode}`;
        })

      Text(`tag=${this.tagText}, slider=${this.sliderValue}`)
        .fontSize(13)
        .fontColor('#526070')
        .width('100%')
    }
    .width('100%')
    .padding(14)
    .backgroundColor('#FFFFFF')
    .borderRadius(8)
  }
}
