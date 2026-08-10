import { Point } from './Point';

export class DemoBadge {
  text: string = 'idle';
  revision: number = 0;

  advance(amount: number): number {
    this.revision += amount;
    return this.revision;
  }
}

export class DemoProfile {
  badge: DemoBadge = new DemoBadge();

  summary(): string {
    return `${this.badge.text}@${this.badge.revision}`;
  }
}

export class DemoViewModel {
  buttonTitle: string = 'idle';
  backgroundColor: string = '#FFFFFF';
  profile: DemoProfile = new DemoProfile();

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

  private hiddenNote(): string {
    return 'hidden-42';
  }

  revealNote(): string {
    return this.hiddenNote();
  }

  static crash(): string {
    throw new Error('class crash');
  }
}
