// Compares a previously published version (e.g. the MkDocs gh-pages output) with the
// current build, so a redesign cannot silently break shared URLs, anchors or assets.
//   node scripts/compare-urls.mjs <old-version-dir> [new-build-dir]
import { readFile, readdir, stat } from 'node:fs/promises';
import path from 'node:path';

const [oldRoot, newRoot = 'dist'] = process.argv.slice(2);
if (!oldRoot) throw new Error('Usage: node scripts/compare-urls.mjs <old-version-dir> [new-build-dir]');

async function* files(directory) {
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) yield* files(file);
    else yield file;
  }
}

const exists = (file) => stat(file).then(() => true, () => false);
const ids = (html) => new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]));
// Shareable anchors: IDs on headings (h1 included, as the previous site linked to page titles).
const headingIds = (html) => [...html.matchAll(/<h[1-6][^>]*\sid="([^"]+)"/g)].map((m) => m[1]);
// Heading text per ID, so an anchor cannot silently move to a different section.
const headings = (html) =>
  new Map(
    [...html.matchAll(/<h([2-6])[^>]*\sid="([^"]+)"[^>]*>([\s\S]*?)<\/h\1>/g)].map((m) => [
      m[2],
      m[3].replace(/<[^>]+>/g, '').replace(/[¶#]/g, '').replace(/&[a-z]+;|&#\d+;/g, ' ').replace(/\s+/g, ' ').trim().toLowerCase(),
    ]),
  );
const meta = (html, pattern) => html.match(pattern)?.[1];
// Theme internals that never formed part of shareable content.
const ignoredFile = /^(assets\/(javascripts|stylesheets)\/|search\/|_astro\/|pagefind\/|sitemap-(index|\d+)\.xml$)/;
const ignoredId = /^_top$/;

const problems = [];
let pages = 0;
let anchors = 0;
for await (const file of files(oldRoot)) {
  const relative = path.relative(oldRoot, file).split(path.sep).join('/');
  if (ignoredFile.test(relative)) continue;
  const target = path.join(newRoot, relative);
  if (!(await exists(target))) {
    problems.push(`missing: ${relative}`);
    continue;
  }
  if (!relative.endsWith('.html') || relative === '404.html') continue;
  pages++;
  const [before, after] = await Promise.all([readFile(file, 'utf8'), readFile(target, 'utf8')]);
  const newIds = ids(after);
  const newHeadings = headings(after);
  for (const [id, text] of headings(before)) {
    if (newHeadings.has(id) && newHeadings.get(id) !== text) {
      problems.push(`anchor moved: ${relative}#${id}: "${text}" -> "${newHeadings.get(id)}"`);
    }
  }
  for (const id of headingIds(before)) {
    if (ignoredId.test(id)) continue;
    anchors++;
    if (!newIds.has(id)) problems.push(`missing anchor: ${relative}#${id}`);
  }
  const oldCanonical = meta(before, /<link rel="canonical" href="([^"]+)"/);
  const newCanonical = meta(after, /<link rel="canonical" href="([^"]+)"/);
  if (oldCanonical && oldCanonical !== newCanonical) {
    problems.push(`canonical changed: ${relative}: ${oldCanonical} -> ${newCanonical}`);
  }
}
if (problems.length) {
  console.error(problems.join('\n'));
  process.exit(1);
}
console.log(`Preserved ${pages} pages, ${anchors} anchors, canonicals and every published asset URL.`);
