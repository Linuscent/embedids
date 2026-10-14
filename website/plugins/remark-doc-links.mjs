import path from 'node:path';

// Author normal repository-relative Markdown links; publish them as version-aware URLs.
export function docLink(url, file, base = '/') {
  if (/^(?:[a-z][a-z\d+.-]*:|\/\/|#)/i.test(url)) return url;
  const match = url.match(/^([^?#]+)\.mdx?([?#].*)?$/);
  if (!match) return url;
  const relative = path.relative(path.resolve('docs'), path.resolve(path.dirname(file), match[1]));
  if (relative.startsWith('..') || path.isAbsolute(relative)) {
    throw new Error(`Documentation link leaves docs/: ${url} in ${file}`);
  }
  const route = relative.split(path.sep).join('/').replace(/(^|\/)index$/, '');
  return `${base}${route}${route && !route.endsWith('/') ? '/' : ''}${match[2] || ''}`;
}

export default function remarkDocLinks({ base = '/' } = {}) {
  return (tree, file) => {
    function walk(node) {
      if ((node.type === 'link' || node.type === 'definition') && node.url) {
        node.url = docLink(node.url, file.path, base);
      }
      node.children?.forEach(walk);
    }
    walk(tree);
  };
}
