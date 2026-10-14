import { execFileSync } from 'node:child_process';

const cache = new Map();

/**
 * First and latest commit dates for a source file, following renames (e.g. .md → .mdx).
 * Needs full history: CI checks out with `fetch-depth: 0`. Uncommitted files have no dates.
 */
export function gitDates(file) {
  if (!file) return {};
  if (cache.has(file)) return cache.get(file);
  let dates = {};
  try {
    const log = execFileSync('git', ['log', '--follow', '--format=%aI', '--', file], {
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore'],
    })
      .split('\n')
      .filter(Boolean);
    if (log.length) dates = { modified: new Date(log[0]), published: new Date(log[log.length - 1]) };
  } catch {
    // Not a git checkout: pages simply omit dates.
  }
  cache.set(file, dates);
  return dates;
}
