import { DemoViewModel } from './DemoViewModel';
import { Point } from './Point';

export class DemoResult {
  readonly title: string;
  readonly rows: Array<string>;

  constructor(title: string, rows: Array<string>) {
    this.title = title;
    this.rows = rows;
  }
}

export class DemoScenario {
  static run(): DemoResult {
    const rows: Array<string> = [];
    const vm = new DemoViewModel();
    const points = [new Point(-0.33, 1.28)];
    const fallback = new Point(0.5, 1.5);

    try {
      const safePoint = vm.locationOf(points, 2, fallback);
      rows.push(`Invalid index -> ${Point.textOf(safePoint)}, title=${vm.buttonTitle}`);
    } catch (err) {
      rows.push(`Invalid index -> ${(err as Error).message}`);
    }

    try {
      const realPoint = vm.locationOf(points, 0, fallback);
      rows.push(`Valid index -> ${Point.textOf(realPoint)}, title=${vm.buttonTitle}`);
    } catch (err) {
      rows.push(`Valid index -> ${(err as Error).message}`);
    }

    try {
      rows.push(vm.crashIt());
    } catch (err) {
      rows.push(`Instance -> ${(err as Error).message}`);
    }

    try {
      rows.push(DemoViewModel.crash());
    } catch (err) {
      rows.push(`Class -> ${(err as Error).message}`);
    }

    return new DemoResult('OhosPatch prototype hook', rows);
  }
}
