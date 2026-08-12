import fixitTest from './Fixit.test';
import syntaxErrorTest from './SyntaxError.test';
import perfTest from './Perf.test';

export default function testsuite(): void {
  perfTest();
  fixitTest();
  syntaxErrorTest();
}
