!(function (Fixit, require) {
  var DemoViewModel = require(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/DemoViewModel#DemoViewModel'
  );
  var fix = Fixit.fix(DemoViewModel);
  var PatchablePanel = require(
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
      this.buttonTitle = 'out of bounds';
      this.backgroundColor = '#FDE2E2';
      return point;
    }

    this.buttonTitle = 'in bounds';
    this.backgroundColor = '#E7F7EE';
    return originLocation.apply(this, arguments);
  });

  fix.instanceMethod('crashIt', function () {
    return 'Instance method fixed by remote JSVM patch; timer=' + timerState;
  });

  fix.classMethod('crash', function () {
    return 'Class method fixed by remote JSVM patch';
  });
})(Fixit, require);
