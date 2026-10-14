import { defineCollection } from 'astro:content';
import { z } from 'astro/zod';
import { glob } from 'astro/loaders';
import { docsSchema } from '@astrojs/starlight/schema';

export const collections = {
  docs: defineCollection({
    loader: glob({ base: './docs', pattern: '**/*.{md,mdx}' }),
    schema: docsSchema({
      extend: z.object({
        schema_type: z.enum(['TechArticle', 'CollectionPage']).optional(),
        keywords: z.array(z.string()).optional(),
        // Heading IDs from the previous site that must keep resolving on this page.
        legacyAnchors: z.array(z.string()).optional(),
      }),
    }),
  }),
};
