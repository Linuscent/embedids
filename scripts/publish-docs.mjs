import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

export async function publishDocs({ source, destination, version }) {
  if (!/^(main|v\d+\.\d+(?:\.\d+)?)$/.test(version)) throw new Error('Invalid documentation version');
  const manifest = path.join(destination, 'versions.json');
  let versions;
  try {
    versions = JSON.parse(await readFile(manifest, 'utf8'));
    if (!Array.isArray(versions)) throw new Error('Invalid versions.json');
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
    versions = [];
  }
  // Confirm the build exists before replacing this version. Other versions and CNAME survive.
  await readFile(path.join(source, 'index.html'));
  const target = path.join(destination, version);
  await mkdir(destination, { recursive: true });
  await rm(target, { recursive: true, force: true });
  await cp(source, target, { recursive: true });
  if (!versions.some((item) => item.version === version)) {
    versions.push({ version, title: version, aliases: [] });
  }
  await writeFile(manifest, JSON.stringify(versions, null, 2) + '\n');
  await writeFile(path.join(destination, '.nojekyll'), '');
  if (version === 'main') {
    await writeFile(path.join(destination, 'index.html'), `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>EmbedIDS documentation</title>
<link rel="canonical" href="https://embedids.linuscent.com/main/">
<meta http-equiv="refresh" content="0;url=/main/">
<script>location.replace('/main/' + location.search + location.hash)</script>
</head><body><a href="/main/">Open EmbedIDS documentation</a></body></html>\n`);
    await cp(path.join(source, '404.html'), path.join(destination, '404.html'));
    await writeFile(path.join(destination, 'robots.txt'), 'User-agent: *\nAllow: /\n\nSitemap: https://embedids.linuscent.com/sitemap.xml\n');
    // Same root index as the previous site: it lists the default version's URL set, which
    // the build publishes at <version>/sitemap.xml (a <urlset>, so nothing is nested).
    await writeFile(path.join(destination, 'sitemap.xml'), `<?xml version="1.0" encoding="UTF-8"?>
<sitemapindex xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"><sitemap><loc>https://embedids.linuscent.com/main/sitemap.xml</loc></sitemap></sitemapindex>\n`);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const destination = process.argv[2];
  if (!destination) throw new Error('Usage: node scripts/publish-docs.mjs <gh-pages checkout>');
  await publishDocs({ source: 'dist', destination, version: process.env.DOCS_VERSION });
}
