import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// https://astro.build/config
export default defineConfig({
	integrations: [
		starlight({
			title: 'Spud Docs',
			social: {
				github: 'https://github.com/tashcan/spud',
			},
			sidebar: [
				{
					label: 'Guides',
					items: [
						{ label: 'Getting Started', link: '/guides/getting-started/' },
						{ label: 'Detour', link: '/guides/detour/' },
						{ label: 'signature Search', link: '/guides/signature-search/' },
						// { label: 'Memory Manipulation', link: '/guides/memory/' },
						// { label: 'VTable Patching', link: '/guides/vtable/' },
						// { label: 'Import Table Patching', link: '/guides/import-table/' },
					],
				},
				// {
				// 	label: 'Reference',
				// 	autogenerate: { directory: 'reference' },
				// },
			],
			customCss: [
				// Relative path to your custom CSS file
				'./src/styles/custom.css',
			],
		}),
	],
});
