/// <reference path="../../../../../skills/ohospatch/references/fixit.d.js" />

(function (Fixit) {
  // --- Import: 跨模块加载 ArkTS 类（返回持久 Proxy，可 new / 静态 / 实例调用）---
  var Point = Fixit.import(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/Point#Point'
  );

  // --- Patch 目标 ---
  var fix = Fixit.fix(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/DemoViewModel#DemoViewModel'
  );
  var panel = Fixit.component(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/PatchablePanel#PatchablePanel'
  );
  var panelV2 = Fixit.component(
    'com.rickytan.ohospatch/entry/src/main/ets/demo/PatchablePanelV2#PatchablePanelV2'
  );

  // --- Timer + console 日志 ---
  // setTimeout：一次性回调，闭包变量在 method patch 中可读。
  // setInterval：周期回调，到达上限后 clearInterval 自清理。
  // console.*：转发到 HiLog，覆盖 log / info / warn 多个 level。
  var timerState = 'pending';
  var tick = 0;

  console.log('OhosPatch demo patch loaded');
  setTimeout(function () {
    timerState = 'fired';
    console.info('OhosPatch setTimeout callback fired');
  }, 100);

  var intervalId = setInterval(function () {
    tick += 1;
    console.info('OhosPatch interval tick=' + tick);
    if (tick >= 3) {
      clearInterval(intervalId);
      console.warn('OhosPatch interval cleared after ' + tick + ' ticks');
    }
  }, 60);

  // --- Component value override: param / state ---
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

  // --- Component attribute: 静态值 ---
  panel.node({ type: 'Text', occurrence: 0 })
    .attrs({
      fontColor: '#C44736'
    });

  // --- Component attribute handler: 动态属性处理器（函数形式）---
  // 函数在每次渲染时被调用，this 绑定到当前组件实例 Proxy，
  // 返回值作为属性参数。可与静态值混合在同一 attrs 调用中。
  panel.node({ type: 'Text', occurrence: 2 })
    .attrs({
      backgroundColor: '#E7F7EE',
      fontColor: function () { return this.tapCount > 45 ? '#C44736' : '#1F6B46'; }
    });
  panel.node({ type: 'Text', occurrence: 3 })
    .attrs({
      fontSize: function () { return this.switchOn ? 16 : 14; }
    });

  // --- Component event handler: 替换事件回调，origin.apply 委托原始实现 ---
  var originPrimaryClick = panel.node({ type: 'Button', occurrence: 0 })
    .attrs({
      height: 52,
      backgroundColor: '#C44736'
    })
    .event('onClick', /** @this {any} */ function () {
      this.tagText = 'tapCount=' + this.tapCount;
      this.markPrimary(10);
      return originPrimaryClick.apply(this, arguments);
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

  var originUnsafeClick = panel.node({ type: 'Button', occurrence: 2 })
    .attrs({
      height: 52,
      backgroundColor: '#1F6B46'
    })
    .event('onClick', /** @this {any} */ function () {
      try {
        return originUnsafeClick.apply(this, arguments);
      } catch (err) {
        var message = err && err.message ? err.message : String(err);
        this.tagText = 'Recovered Button.onClick crash: ' + message;
        this.statusText = 'Patched Button.onClick recovered';
      }
    });

  var originToggleChange = panel.node({ type: 'Toggle', occurrence: 0 })
    .event('onChange', /** @this {any} */ function (isOn) {
      this.tagText = 'toggle patched before origin';
      this.statusText = 'Patched toggle received ' + isOn;
      return originToggleChange.apply(this, arguments);
    });

  // --- ComponentV2: 与 ComponentV1 使用相同 DSL ---
  panelV2.param('message').replace('Patched V2 component parameter');
  panelV2.state('tapCount').replace(100);
  panelV2.state('statusText').replace('V2 state replaced before initial render');
  panelV2.node({ type: 'Text', occurrence: 0 })
    .attr('fontColor', '#C44736');
  panelV2.node({ type: 'Text', occurrence: 1 })
    .attr('backgroundColor', function () {
      return this.tapCount >= 100 ? '#E7F7EE' : '#EEF2F5';
    });
  var originV2Click = panelV2.node({ type: 'Button', occurrence: 0 })
    .attrs({
      height: 52,
      backgroundColor: '#1F6B46'
    })
    .event('onClick', /** @this {any} */ function () {
      this.markPrimary(10);
      var result = originV2Click.apply(this, arguments);
      this.statusText = 'Patched V2 event, taps=' + this.tapCount;
      return result;
    });

  // --- Method patch: 实例方法 ---
  // locationOf：越界时改写 this 上的嵌套属性并返回默认值；命中时委托 origin。
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

  // crashIt：原实现抛错，patch 后返回正常字符串并读取 timer 闭包状态。
  fix.instanceMethod('crashIt', function () {
    return 'Instance method fixed by remote JSVM patch; timer=' + timerState +
      ', ticks=' + tick;
  });

  // 私有方法 + 同类方法互调：hiddenNote 被 revealNote 通过 this 调用。
  fix.instanceMethod('hiddenNote', function () {
    return 'hidden-50';
  });
  fix.instanceMethod('revealNote', function () {
    return this.hiddenNote() + ':patched';
  });

  // --- Method patch: 类方法（静态）+ import 跨模块类调用 ---
  // new Point() 构造、Point.textOf() 静态、point.toText() 实例，
  // 全部走 import 返回的持久 Proxy。
  fix.classMethod('crash', function () {
    var point = new Point(7, 9);
    return 'Class method fixed by remote JSVM patch; static=' + Point.textOf(point) +
      ', instance=' + point.toText();
  });
})(Fixit);
