!(function (Fixit) {
  var fix = Fixit.fix({
    className: 'DemoViewModel',
    modulePath: 'ets/demo/DemoViewModel',
    exportName: 'DemoViewModel'
  });

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
    return 'Instance method fixed by remote JSVM patch';
  });

  fix.classMethod('crash', function () {
    return 'Class method fixed by remote JSVM patch';
  });
})(Fixit);
