import { readFile, readdir, stat } from 'node:fs/promises';
import path from 'node:path';

const root = path.resolve('dist');
const base = process.env.DOCS_VERSION ? `/${process.env.DOCS_VERSION}/` : '/';
const origin = 'https://embedids.linuscent.com';
const errors = [];

async function* htmlFiles(directory) {
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) yield* htmlFiles(file);
    else if (entry.name.endsWith('.html')) yield file;
  }
}

const documents = new Map();
for await (const file of htmlFiles(root)) documents.set(file, await readFile(file, 'utf8'));
if (documents.size < 6) throw new Error('Build the complete site before checking links.');

for (const [file, html] of documents) {
  const source = path.relative(root, file);
  const pageUrl = new URL(base + source.replace(/index\.html$/, ''), origin);
  for (const tag of html.matchAll(/<(?:a|img|script|link|source)\b[^>]*>/g)) {
    const value = tag[0].match(/\b(?:href|src)="([^"]+)"/)?.[1];
    if (!value) continue;
    // Astro emits /404/ as the error page's canonical, but writes 404.html for static hosts.
    if (source === '404.html' && /\brel="canonical"/.test(tag[0])) continue;
    const url = new URL(value.replaceAll('&amp;', '&'), pageUrl);
    if (url.origin !== origin) continue;
    // The latest-docs link is outside release builds, at the domain root.
    if (base !== '/' && url.pathname === '/main/' && base !== '/main/') continue;
    if (!url.pathname.startsWith(base)) {
      errors.push(`${source}: URL escapes deployment base: ${value}`);
      continue;
    }
    let target = path.join(root, decodeURIComponent(url.pathname.slice(base.length)));
    try {
      if ((await stat(target)).isDirectory()) target = path.join(target, 'index.html');
      await stat(target);
      if (url.hash && documents.has(target)) {
        const id = decodeURIComponent(url.hash.slice(1));
        const ids = [...documents.get(target).matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);
        if (!ids.includes(id)) errors.push(`${source}: missing anchor ${value}`);
      }
    } catch {
      errors.push(`${source}: missing target ${value}`);
    }
  }
}
if (errors.length) throw new Error(errors.join('\n'));
console.log(`Checked links, anchors and assets in ${documents.size} pages (base ${base}).`);
