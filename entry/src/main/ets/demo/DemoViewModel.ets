import { Point } from './Point';

export class DemoViewModel {
  buttonTitle: string = 'idle';
  backgroundColor: string = '#FFFFFF';

  locationOf(locations: Array<Point>, index: number, _defaultValue: Point): Point {
    const point = locations[index];
    if (point === undefined) {
      throw new Error(`index ${index} out of bounds`);
    }
    return point;
  }

  crashIt(): string {
    throw new Error('instance crash');
  }

  static crash(): string {
    throw new Error('class crash');
  }
}
