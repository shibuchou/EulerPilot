import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createConsoleServer } from './server.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(here, '..', '..', '..');
process.chdir(rootDir);

const { server, config } = createConsoleServer();
server.listen(config.port, config.host, () => {
  console.log(`EulerPilot Web Console listening on http://${config.host}:${config.port}`);
  console.log(`root=${config.rootDir}`);
});
