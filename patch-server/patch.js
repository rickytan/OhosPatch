/// <reference path="../skills/ohospatch/references/fixit.d.js" />

(function (Fixit) {
  var Point = Fixit.import(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/Point#Point'
  );
  var fix = Fixit.fix(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/DemoViewModel#DemoViewModel'
  );
  var panel = Fixit.component(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/PatchablePanel#PatchablePanel'
  );
  var timerState = 'pending';

  panel.param('message').replace('Patched component parameter');
  panel.param('subtitle').replace('Patched subtitle from remote JavaScript');
  panel.state('tapCount').transform(function (value) {
    return value === 0 ? 40 : value;
  });
  panel.state('statusText').replace('State replaced while the component was created');
  panel.state('secondaryCount').transform(function (value) {
    return value + 20;
  });
  panel.state('switchOn').replace(true);
  panel.state('sliderValue').replace(75);

  panel.node({ type: 'Text', occurrence: 0 })
    .attrs({
      fontColor: '#C44736'
    });
  panel.node({ type: 'Text', occurrence: 2 })
    .attrs({
      backgroundColor: '#E7F7EE',
      fontColor: '#1F6B46'
    });

  var originPrimaryClick = panel.node({ type: 'Button', occurrence: 0 })
    .attrs({
      height: 52,
      backgroundColor: '#C44736'
    })
    .event('onClick', {
      mode: 'replace',
      capture: ['tapCount'],
      handler: /** @this {any} */ function (_event, context) {
        this.tagText = 'captured tapCount=' + context.state.tapCount;
        this.markPrimary(10);
        return originPrimaryClick.apply(this, arguments);
      }
    });

  var originSecondaryClick = panel.node({ type: 'Button', occurrence: 1 })
    .attrs({
      height: 52,
      backgroundColor: '#1F6B46'
    })
    .event('onClick', /** @this {any} */ function () {
      this.statusText = this.markSecondary('patched');
      return originSecondaryClick.apply(this, arguments);
    });

  var originToggleChange = panel.node({ type: 'Toggle', occurrence: 0 })
    .event('onChange', /** @this {any} */ function (isOn) {
      this.tagText = 'toggle patched before origin';
      this.statusText = 'Patched toggle received ' + isOn;
      return originToggleChange.apply(this, arguments);
    });

  setTimeout(function () {
    timerState = 'fired';
    console.info('OhosPatch setTimeout callback fired');
  }, 100);

  var originLocation = fix.instanceMethod('locationOf', function (locations, index, point) {
    if (index < 0 || index >= locations.length) {
      this.profile.badge.text = 'out of bounds';
      this.profile.badge.advance(10);
      this.buttonTitle = this.profile.summary();
      this.backgroundColor = '#FDE2E2';
      return point;
    }

    this.profile.badge.text = 'in bounds';
    this.profile.badge.advance(1);
    this.buttonTitle = this.profile.summary();
    this.backgroundColor = '#E7F7EE';
    return originLocation.apply(this, arguments);
  });

  fix.instanceMethod('crashIt', function () {
    return 'Instance method fixed by remote JSVM patch; timer=' + timerState;
  });

  fix.classMethod('crash', function () {
    var point = new Point(7, 9);
    return 'Class method fixed by remote JSVM patch; static=' + Point.textOf(point) +
      ', instance=' + point.toText();
  });
  fix.instanceMethod('hiddenNote', function () {
    return 'hidden-50';
  });
  fix.instanceMethod('revealNote', function () {
    return this.hiddenNote() + ':patched';
  });
})(Fixit);
