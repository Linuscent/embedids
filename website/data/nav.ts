// Top menu. Links to pages are relative to the deployment base (e.g. /main/).
export type NavLink = {
  label: string;
  description: string;
  href: string;
  icon: string;
  external?: boolean;
};
export type NavItem = { label: string; href?: string; match?: string; children?: NavLink[] };

export const mainNav: NavItem[] = [
  { label: 'Overview', href: '', match: '' },
  {
    label: 'Get started',
    match: 'quickstart|tutorial',
    children: [
      { label: 'Quick Start', description: 'Get running in 5 minutes', href: 'quickstart/', icon: 'rocket' },
      { label: 'Tutorial', description: 'Comprehensive learning guide', href: 'tutorial/', icon: 'open-book' },
    ],
  },
  {
    label: 'Case studies',
    match: 'case-studies',
    children: [
      {
        label: 'All case studies',
        description: 'EmbedIDS applied to embedded and edge security problems',
        href: 'case-studies/',
        icon: 'list-format',
      },
      {
        label: 'Runtime Security for Edge AI',
        description: 'EmbedIDS protecting a Jetson-class AI device',
        href: 'case-studies/edgeml-runtime-security/',
        icon: 'star',
      },
      {
        label: 'Intrusion Detection on Zephyr',
        description: 'EmbedIDS compiled into Zephyr RTOS firmware',
        href: 'case-studies/zephyr-runtime-security/',
        icon: 'puzzle',
      },
    ],
  },
  {
    label: 'Resources',
    children: [
      {
        label: 'Code examples',
        description: 'Ready-to-run code samples',
        href: 'https://github.com/Linuscent/EmbedIDS/tree/main/examples',
        icon: 'document',
        external: true,
      },
      {
        label: 'GitHub repository',
        description: 'Source code and issues',
        href: 'https://github.com/Linuscent/EmbedIDS',
        icon: 'github',
        external: true,
      },
      {
        label: 'Linuscent',
        description: 'The company behind EmbedIDS',
        href: 'https://linuscent.com',
        icon: 'external',
        external: true,
      },
    ],
  },
];

export const resolveHref = (href: string, base: string) =>
  /^https?:\/\//.test(href) ? href : `${base}${href}`;

/** True when the current page belongs to this menu item. */
export function isActive(item: NavItem, pathname: string, base: string) {
  const path = pathname.startsWith(base) ? pathname.slice(base.length).replace(/\/$/, '') : pathname;
  if (item.match === undefined) return false;
  if (item.match === '') return path === '';
  return new RegExp(`^(${item.match})(/|$)`).test(path);
}
