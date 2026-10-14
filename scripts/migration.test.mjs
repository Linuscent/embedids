import assert from 'node:assert/strict';
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { docLink } from '../website/plugins/remark-doc-links.mjs';
import { publishDocs } from './publish-docs.mjs';

test('repository links retain routes and anchors under root and versioned deployments', () => {
  for (const base of ['/', '/main/', '/v0.1/']) {
    assert.equal(docLink('../quickstart.md#installation', 'docs/case-studies/index.md', base), `${base}quickstart/#installation`);
    assert.equal(docLink('index.md', 'docs/case-studies/edgeml-runtime-security.mdx', base), `${base}case-studies/`);
    assert.equal(docLink('edgeml-runtime-security.mdx', 'docs/case-studies/index.md', base), `${base}case-studies/edgeml-runtime-security/`);
    assert.equal(docLink('index.md', 'docs/index.mdx', base), base);
    assert.equal(docLink('https://github.com/a/README.md', 'docs/index.mdx', base), 'https://github.com/a/README.md');
  }
  assert.throws(() => docLink('../README.md', 'docs/index.mdx'), /leaves docs/);
});

test('publishing retains archives, aliases and custom domain', async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'embedids-publish-'));
  try {
    const source = path.join(root, 'dist');
    const destination = path.join(root, 'pages');
    await mkdir(source);
    await mkdir(path.join(destination, 'v0.1'), { recursive: true });
    await mkdir(path.join(destination, 'main'));
    await writeFile(path.join(source, 'index.html'), 'new home');
    await writeFile(path.join(source, '404.html'), 'not found');
    await writeFile(path.join(destination, 'main', 'obsolete.html'), 'old file');
    await writeFile(path.join(destination, 'v0.1', 'index.html'), 'archive');
    await writeFile(path.join(destination, 'CNAME'), 'embedids.linuscent.com');
    await writeFile(path.join(destination, 'versions.json'), JSON.stringify([{ version: 'v0.1', title: '0.1', aliases: ['stable'] }]));
    await publishDocs({ source, destination, version: 'main' });
    assert.equal(await readFile(path.join(destination, 'main', 'index.html'), 'utf8'), 'new home');
    await assert.rejects(readFile(path.join(destination, 'main', 'obsolete.html')));
    assert.equal(await readFile(path.join(destination, 'v0.1', 'index.html'), 'utf8'), 'archive');
    assert.equal(await readFile(path.join(destination, 'CNAME'), 'utf8'), 'embedids.linuscent.com');
    assert.deepEqual(JSON.parse(await readFile(path.join(destination, 'versions.json'), 'utf8'))[0].aliases, ['stable']);
    const sitemap = await readFile(path.join(destination, 'sitemap.xml'), 'utf8');
    // Unchanged from the MkDocs site: the root index lists /main/sitemap.xml.
    assert.match(sitemap, /<loc>https:\/\/embedids\.linuscent\.com\/main\/sitemap\.xml<\/loc>/);
    const rootIndex = await readFile(path.join(destination, 'index.html'), 'utf8');
    await publishDocs({ source, destination, version: 'v0.2' });
    assert.equal(await readFile(path.join(destination, 'index.html'), 'utf8'), rootIndex);
    await assert.rejects(publishDocs({ source, destination, version: '../main' }), /Invalid/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
