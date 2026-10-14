import { defineRouteMiddleware } from '@astrojs/starlight/route-data';
import { gitDates } from './utils/git-dates.mjs';

const version = import.meta.env.DOCS_VERSION;

/** SEO adjustments to every page's route data, applied before rendering. */
export const onRequest = defineRouteMiddleware(({ locals }) => {
  const route = locals.starlightRoute;
  const { entry } = route;
  const isHome = entry.id === '' || entry.id === 'index';
  const is404 = entry.id === '404' || entry.id.endsWith('/404');

  // "Last updated" from git: Starlight's own option assumes src/content/docs/.
  const { modified } = gitDates(entry.filePath);
  if (modified && route.lastUpdated === undefined) route.lastUpdated = modified;

  route.head = route.head
    // An error page has no canonical address of its own.
    .filter((tag) => !(is404 && tag.tag === 'link' && tag.attrs?.rel === 'canonical'))
    .map((tag) => {
      const property = tag.attrs?.property;
      // Open Graph wants language_TERRITORY.
      if (property === 'og:locale') return { ...tag, attrs: { ...tag.attrs, content: 'en_US' } };
      if (property === 'og:type' && isHome) return { ...tag, attrs: { ...tag.attrs, content: 'website' } };
      return tag;
    });

  // Keep error pages, and archived versions that duplicate /main/, out of search results.
  if (is404 || (version && version !== 'main')) {
    route.head.push({ tag: 'meta', attrs: { name: 'robots', content: 'noindex, follow' } });
  }
});
