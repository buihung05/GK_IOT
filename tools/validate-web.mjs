import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const html = await readFile(join(root, 'web', 'index.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
if (!script) throw new Error('Không tìm thấy JavaScript trong dashboard.');
new Function(script);

const requiredIds = ['stateTitle','startButton','continueButton','stopButton','powerChart','configForm','simulationPanel'];
for (const id of requiredIds) {
  if (!html.includes(`id="${id}"`)) throw new Error(`Thiếu phần tử #${id}`);
}
console.log('Dashboard hợp lệ: JavaScript biên dịch được và đủ thành phần chính.');

