import { createReadStream } from 'node:fs';
import { createServer } from 'node:http';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(fileURLToPath(import.meta.url));
const patchPath = join(root, '../entry/src/main/resources/rawfile/patch.js');
const port = Number.parseInt(process.env.PORT || '8080', 10);

createServer((request, response) => {
  if (request.url !== '/patch.js') {
    response.writeHead(404);
    response.end('Not found');
    return;
  }

  response.writeHead(200, {
    'Cache-Control': 'no-store',
    'Content-Type': 'application/javascript; charset=utf-8'
  });
  createReadStream(patchPath).pipe(response);
}).listen(port, '0.0.0.0', () => {
  console.log(`OhosPatch server: http://127.0.0.1:${port}/patch.js`);
  console.log(`Serving ${patchPath}`);
});
