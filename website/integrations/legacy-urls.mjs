import { existsSync } from 'node:fs';
import { cp, readFile, readdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';
import { gitDates } from '../utils/git-dates.mjs';

/** Source file for a published route: quickstart/ -> docs/quickstart.md, '' -> docs/index.mdx. */
function sourceFor(route) {
  const stem = route.replace(/\/$/, '');
  const candidates = stem
    ? [`docs/${stem}.md`, `docs/${stem}.mdx`, `docs/${stem}/index.md`, `docs/${stem}/index.mdx`]
    : ['docs/index.md', 'docs/index.mdx'];
  return candidates.find((file) => existsSync(file));
}

/**
 * Keeps the URLs the previous MkDocs site published, so search results, backlinks and
 * cached share cards keep working:
 *   - <version>/assets/** (figures, logos, favicon, social cards) at their original paths;
 *   - <version>/sitemap.xml (+ .gz), the URL set the root sitemap index points to, with
 *     each page's <lastmod> taken from git.
 */
export default function legacyUrls({ base = '/', archived = false } = {}) {
  return {
    name: 'embedids:legacy-urls',
    hooks: {
      'astro:build:done': async ({ dir }) => {
        await cp(new URL('../../docs/assets/', import.meta.url), new URL('assets/', dir), {
          recursive: true,
        });
        const outDir = fileURLToPath(dir);
        const parts = (await readdir(outDir)).filter((file) => /^sitemap-\d+\.xml$/.test(file)).sort();
        const empty = '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"></urlset>\n';
        if (archived) {
          // Archived versions are noindex: their sitemaps must not advertise their pages.
          for (const file of [...parts, 'sitemap.xml']) await writeFile(new URL(file, dir), empty);
          await writeFile(new URL('sitemap.xml.gz', dir), gzipSync(empty));
          return;
        }
        const urls = [];
        for (const part of parts) {
          const xml = await readFile(new URL(part, dir), 'utf8');
          for (const [entry, loc] of xml.matchAll(/<url>\s*<loc>([^<]+)<\/loc>[\s\S]*?<\/url>/g)) {
            const route = new URL(loc).pathname.slice(base.length);
            const { modified } = gitDates(sourceFor(route));
            urls.push(
              modified
                ? `<url><loc>${loc}</loc><lastmod>${modified.toISOString().slice(0, 10)}</lastmod></url>`
                : entry,
            );
          }
        }
        const sitemap = `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">${urls.join('')}</urlset>\n`;
        await writeFile(new URL('sitemap.xml', dir), sitemap);
        await writeFile(new URL('sitemap.xml.gz', dir), gzipSync(sitemap));
      },
    },
  };
}
