export class Point {
  readonly x: number;
  readonly y: number;

  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }

  toText(): string {
    return `(${this.x}, ${this.y})`;
  }

  static textOf(point: Point): string {
    return `(${point.x}, ${point.y})`;
  }
}
