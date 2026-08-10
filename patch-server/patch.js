/// <reference path="../fixit.d.js" />

(function (Fixit, require) {
  var loadClass = require;
  var DemoViewModel = loadClass(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/DemoViewModel#DemoViewModel'
  );
  var Point = Fixit.import(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/Point#Point'
  );
  var fix = Fixit.fix(DemoViewModel);
  var PatchablePanel = loadClass(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/PatchablePanel#PatchablePanel'
  );
  var panel = Fixit.component(PatchablePanel);
  var timerState = 'pending';

  panel.param('message').replace('Patched component parameter');
  panel.state('tapCount').transform(function (value) {
    return value === 0 ? 40 : value;
  });
  panel.node({ type: 'Button', occurrence: 0 })
    .attrs({
      height: 52,
      backgroundColor: '#C44736'
    })
    .event('onClick', {
      mode: 'replace',
      capture: ['tapCount'],
      handler: function (_event, context) {
        context.setState({ tapCount: context.state.tapCount + 10 });
      }
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
})(Fixit, require);
