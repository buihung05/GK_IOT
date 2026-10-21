import { readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const html = await readFile(join(root, 'web', 'index.html'), 'utf8');

if (html.includes(')EVWEB"')) {
  throw new Error('Nội dung HTML trùng với dấu kết thúc raw string EVWEB.');
}

const header = `// File được tạo tự động bởi tools/embed-web.mjs. Không sửa trực tiếp.\n#pragma once\n\n#include <pgmspace.h>\n\nstatic const char INDEX_HTML[] PROGMEM = R"EVWEB(${html})EVWEB";\n`;
await writeFile(join(root, 'src', 'WebPage.h'), header, 'utf8');
console.log(`Đã nhúng ${Buffer.byteLength(html)} byte HTML vào src/WebPage.h`);

