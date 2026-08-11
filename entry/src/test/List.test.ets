import fixitTest from '../ohosTest/ets/test/Fixit.test';
import syntaxErrorTest from '../ohosTest/ets/test/SyntaxError.test';
import nativeSafetyTest from '../ohosTest/ets/test/NativeSafety.test';
import declarationTest from '../ohosTest/ets/test/Declaration.test';
import perfTest from '../ohosTest/ets/test/Perf.test';

export default function testsuite(): void {
  fixitTest();
  syntaxErrorTest();
  nativeSafetyTest();
  declarationTest();
  perfTest();
}
