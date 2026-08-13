/// <reference path="../../../../../skills/ohospatch/references/fixit.d.js" />

(function (Fixit) {
  // --- Import: 跨模块加载 ArkTS 类（返回持久 Proxy，可 new / 静态 / 实例调用）---
  var Point = Fixit.import(
    '@vendor/business_page/src/main/ets/ViewModel/Point#Point'
  );

  // --- Patch 目标 ---
  var fix = Fixit.fix(
    '@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel'
  );
  var panel = Fixit.component(
    '@vendor/business_page/src/main/ets/components/PatchablePanel#PatchablePanel'
  );
  var panelV2 = Fixit.component(
    '@vendor/business_page/src/main/ets/components/PatchablePanelV2#PatchablePanelV2'
  );
  var builderParamRoot = Fixit.component(
    '@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamRoot'
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
  panel.param('message', 'Patched component parameter');
  panel.param('subtitle', function (originValue) {
    this.tagText = 'Original subtitle: ' + originValue;
    return 'Patched subtitle from remote JavaScript';
  });
  panel.state('tapCount', function (value) {
    return value === 0 ? 40 : value;
  });
  panel.state('statusText', 'State replaced while the component was created');
  panel.state('secondaryCount', function (value) {
    return value + 20;
  });
  panel.state('switchOn', true);
  panel.state('sliderValue', 75);

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

  // Slider.onChange 有 value、mode 两个参数；handler 按原顺序接收，arguments 原样转发给原回调。
  var originSliderChange = panel.node({ type: 'Slider', occurrence: 0 })
    .event('onChange', /** @this {any} */ function (value, mode) {
      this.tagText = 'Patched slider value=' + value + ', mode=' + mode;
      return originSliderChange.apply(this, arguments);
    });

  // --- ComponentV2: 与 ComponentV1 使用相同 DSL ---
  panelV2.param('message', function (originValue) {
    this.statusText = 'V2 param handler received ' + originValue;
    return 'Patched V2 component parameter';
  });
  panelV2.state('tapCount', function (originValue) {
    this.statusText = 'V2 state handler received ' + originValue;
    return 100;
  });
  panelV2.node({ type: 'Text', occurrence: 0 })
    .attr('fontColor', '#C44736');
  panelV2.node({ type: 'Text', occurrence: 1 })
    .attr('backgroundColor', function () {
      return this.tapCount >= 100 ? '#E7F7EE' : '#EEF2F5';
    });
  // id 位于宿主 builder 链尾；where 仍会读取原始 id/height 并只命中第一个符合节点。
  var originV2Click = panelV2.node({
    type: 'Button',
    where: { id: 'v2-primary-action', height: 44 }
  })
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

  Fixit.component(
    '@vendor/business_page/src/main/ets/components/ChildParamPanel#ParentChildParamPanel'
  )
    .node({
      type: '@vendor/business_page/src/main/ets/components/ChildParamPanel#ChildParamPanel',
      occurrence: 1
    })
    .param('title', function (originValue) {
      return 'scoped patched ' + originValue;
    });

  // --- BuilderParam: patch nodes built by trailing and explicitly passed builders.
  builderParamRoot.param('message', 'Patched builder-param root parameter');
  builderParamRoot.state('statusText', 'Patched builder-param root state');
  builderParamRoot.node({
    type: '@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamShell',
    occurrence: 0
  })
    .builder({ type: 'Text', occurrence: 0 })
    .attrs({
      fontColor: '#C44736',
      fontSize: 18
    });
  var originBuilderSlotClick = builderParamRoot.node({
    type: '@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamShell',
    occurrence: 0
  })
    .builder({ type: 'Button', occurrence: 0 })
    .attrs({
      backgroundColor: '#C44736',
      height: 52
    })
    .event('onClick', /** @this {any} */ function () {
      var result = originBuilderSlotClick.apply(this, arguments);
      this.statusText = 'Patched builder slot click';
      return result;
    });
  builderParamRoot.node({
    type: '@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamShell',
    occurrence: 1
  })
    .builder({ type: 'Text', occurrence: 0 })
    .attrs({
      fontColor: '#1F6B46',
      fontSize: 18
    });
  var originInlineBuilderSlotClick = builderParamRoot.node({
    type: '@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamShell',
    occurrence: 1
  })
    .builder({ type: 'Button', occurrence: 0 })
    .attrs({
      backgroundColor: '#1F6B46',
      height: 52
    })
    .event('onClick', /** @this {any} */ function () {
      var result = originInlineBuilderSlotClick.apply(this, arguments);
      this.statusText = 'Patched inline builder slot click';
      return result;
    });
  var multiBuilderNode = builderParamRoot.node({
    type: '@vendor/business_page/src/main/ets/components/BuilderParamPanel#MultiBuilderParamShell',
    occurrence: 0
  });
  multiBuilderNode
    .builder('headerBuilder', { type: 'Text', occurrence: 0 })
    .attrs({ fontColor: '#8A3FFC', fontSize: 18 });
  multiBuilderNode
    .builder('contentBuilder', { type: 'Text', occurrence: 0 })
    .attrs({ fontColor: '#007D8A', fontSize: 18 });
  var originMultiContentClick = multiBuilderNode
    .builder('contentBuilder', { type: 'Button', occurrence: 0 })
    .attrs({ backgroundColor: '#007D8A', height: 52 })
    .event('onClick', /** @this {any} */ function () {
      var result = originMultiContentClick.apply(this, arguments);
      this.statusText = 'Patched explicit contentBuilder click';
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
