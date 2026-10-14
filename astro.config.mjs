import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import remarkDocLinks from './website/plugins/remark-doc-links.mjs';
import rehypeLeadImage from './website/plugins/rehype-lead-image.mjs';
import legacyUrls from './website/integrations/legacy-urls.mjs';

// Keep the published /main/ and /vX.Y/ URLs used by the previous site.
const version = process.env.DOCS_VERSION;
if (version && !/^(main|v\d+\.\d+(?:\.\d+)?)$/.test(version)) {
  throw new Error('DOCS_VERSION must be main or vX.Y[.Z]');
}
const base = version ? `/${version}/` : '/';

export default defineConfig({
  site: 'https://embedids.linuscent.com',
  srcDir: './website',
  base,
  trailingSlash: 'always',
  markdown: {
    remarkPlugins: [[remarkDocLinks, { base }]],
    rehypePlugins: [rehypeLeadImage],
  },
  integrations: [
    starlight({
      title: 'EmbedIDS',
      // Same "Page - EmbedIDS" titles as the previous site, so search snippets do not change.
      titleDelimiter: '-',
      description: 'Lightweight, open-source runtime intrusion detection for embedded and IoT devices. On-device anomaly detection with no dynamic memory allocation.',
      favicon: '/favicon.png',
      // SEO: canonical/locale/type fixes, noindex for 404 and archived versions, git dates.
      routeMiddleware: './website/route-data.ts',
      customCss: ['./website/styles/brand.css', './website/styles/home.css'],
      social: [
        { icon: 'github', label: 'EmbedIDS on GitHub', href: 'https://github.com/Linuscent/EmbedIDS' },
        { icon: 'linkedin', label: 'Linuscent on LinkedIn', href: 'https://www.linkedin.com/company/linuscent' },
      ],
      editLink: { baseUrl: `https://github.com/Linuscent/EmbedIDS/edit/${version || 'main'}/` },
      components: {
        SiteTitle: './website/components/SiteTitle.astro',
        Header: './website/components/Header.astro',
        Footer: './website/components/Footer.astro',
        Head: './website/components/Head.astro',
        Hero: './website/components/Hero.astro',
        PageTitle: './website/components/PageTitle.astro',
        ThemeProvider: './website/components/ThemeProvider.astro',
        MobileMenuToggle: './website/components/MobileMenuToggle.astro',
      },
      sidebar: [
        { label: 'Overview', slug: '' },
        { label: 'Get started', items: [
          { label: 'Quick Start', slug: 'quickstart' },
          { label: 'Tutorial', slug: 'tutorial' },
        ] },
        { label: 'Case studies', items: [
          { label: 'All case studies', slug: 'case-studies' },
          { label: 'Runtime Security for Edge AI', slug: 'case-studies/edgeml-runtime-security' },
        ] },
        { label: 'Resources', items: [
          { label: 'Code examples', link: 'https://github.com/Linuscent/EmbedIDS/tree/main/examples' },
          { label: 'Linuscent', link: 'https://linuscent.com' },
        ] },
      ],
      expressiveCode: { themes: ['github-dark', 'github-light'] },
    }),
    legacyUrls({ base, archived: Boolean(version && version !== 'main') }),
  ],
});
