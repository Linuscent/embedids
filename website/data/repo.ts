// Links to source on the repository's main branch: files open as blobs, directories
// (paths ending in "/") as trees.
export const repo = 'https://github.com/Linuscent/EmbedIDS';

export const sourceUrl = (path: string) =>
  path.endsWith('/') ? `${repo}/tree/main/${path.slice(0, -1)}` : `${repo}/blob/main/${path}`;
