// Fails the build when a page breaks the SEO rules this site follows.
//   DOCS_VERSION=main pnpm build && DOCS_VERSION=main pnpm check:seo
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import path from 'node:path';

const root = path.resolve('dist');
const version = process.env.DOCS_VERSION;
const base = version ? `/${version}/` : '/';
const origin = 'https://embedids.linuscent.com';
const archived = Boolean(version && version !== 'main');

const pages = [];
(function walk(dir) {
  for (const name of readdirSync(dir)) {
    const file = path.join(dir, name);
    if (statSync(file).isDirectory()) walk(file);
    else if (name.endsWith('.html')) pages.push(file);
  }
})(root);

const problems = [];
const all = (html, re) => [...html.matchAll(re)].map((m) => m[1]);
const sitemap = readFileSync(path.join(root, 'sitemap.xml'), 'utf8');
const listed = new Set(all(sitemap, /<loc>([^<]+)<\/loc>/g));
const titles = new Map();
const descriptions = new Map();

for (const file of pages) {
  const rel = path.relative(root, file);
  const url = `${origin}${base}${rel.replace(/index\.html$/, '')}`;
  const html = readFileSync(file, 'utf8');
  const head = html.slice(0, html.indexOf('</head>'));
  const fail = (message) => problems.push(`${rel}: ${message}`);
  const is404 = rel === '404.html';

  const title = all(head, /<title>([^<]*)<\/title>/g);
  if (title.length !== 1 || !title[0].trim()) fail('needs exactly one non-empty <title>');
  const description = all(head, /<meta name="description" content="([^"]*)"/g);
  if (description.length !== 1) fail('needs exactly one meta description');
  else if (!is404 && (description[0].length < 50 || description[0].length > 170)) {
    fail(`description is ${description[0].length} characters (aim for 50–160)`);
  }
  if (!/<html[^>]* lang="en"/.test(html)) fail('<html> needs lang="en"');
  if ((html.match(/<h1[\s>]/g) ?? []).length !== 1) fail('needs exactly one <h1>');

  for (const property of ['og:title', 'og:description', 'og:type', 'og:locale', 'og:image', 'twitter:card']) {
    const count = (head.match(new RegExp(`(property|name)="${property}"`, 'g')) ?? []).length;
    if (count !== 1) fail(`needs exactly one ${property} (found ${count})`);
  }
  const image = all(head, /property="og:image" content="([^"]+)"/g)[0];
  if (image) {
    if (!image.startsWith(`${origin}${base}`)) fail(`og:image must be an absolute URL on this site: ${image}`);
    else if (!existsSync(path.join(root, decodeURIComponent(image.slice(`${origin}${base}`.length))))) {
      fail(`og:image does not exist: ${image}`);
    }
  }
  if (/<img(?![^>]*\balt=)[^>]*>/.test(html)) fail('every <img> needs an alt attribute');
  if (/<img(?![^>]*\bwidth=)[^>]*>/.test(html)) fail('every <img> needs width and height (layout shift)');

  const robots = all(head, /<meta name="robots" content="([^"]+)"/g)[0] ?? '';
  const canonical = all(head, /<link rel="canonical" href="([^"]+)"/g);
  if (is404 || archived) {
    if (!robots.includes('noindex')) fail('must be noindex (error page or archived version)');
    if (listed.has(url)) fail('noindex page must not be in sitemap.xml');
  } else {
    if (robots.includes('noindex')) fail('indexable page is marked noindex');
    if (canonical.length !== 1 || canonical[0] !== url) fail(`canonical must be ${url} (found ${canonical.join(', ')})`);
    if (!listed.has(url)) fail('missing from sitemap.xml');
    titles.set(title[0], [...(titles.get(title[0]) ?? []), rel]);
    descriptions.set(description[0], [...(descriptions.get(description[0]) ?? []), rel]);
  }

  for (const json of all(html, /<script type="application\/ld\+json">([^<]*)<\/script>/g)) {
    try {
      const data = JSON.parse(json);
      if (data['@context'] !== 'https://schema.org') fail('JSON-LD needs @context https://schema.org');
    } catch {
      fail('invalid JSON-LD');
    }
  }
}

for (const [map, label] of [[titles, 'title'], [descriptions, 'description']]) {
  for (const [value, files] of map) if (files.length > 1) problems.push(`duplicate ${label} "${value}": ${files.join(', ')}`);
}
for (const loc of listed) if (!loc.startsWith(`${origin}${base}`)) problems.push(`sitemap URL outside this version: ${loc}`);

if (problems.length) {
  console.error(problems.join('\n'));
  process.exit(1);
}
console.log(`SEO checks passed for ${pages.length} pages (base ${base}).`);
