/**
 * Marks the first image of each page as the likely Largest Contentful Paint element:
 * load it straight away instead of lazily (Astro's default for Markdown images).
 */
export default function rehypeLeadImage() {
  return (tree) => {
    let found = false;
    const visit = (node) => {
      if (found) return;
      if (node.type === 'element' && node.tagName === 'img') {
        node.properties = { ...node.properties, loading: 'eager', fetchpriority: 'high' };
        found = true;
        return;
      }
      if (node.type === 'mdxJsxFlowElement' || node.type === 'mdxJsxTextElement') {
        // Images inside tabs or other components are not the lead image.
        return;
      }
      node.children?.forEach(visit);
    };
    visit(tree);
  };
}
