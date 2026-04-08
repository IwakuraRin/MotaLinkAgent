/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{vue,js,ts}'],
  theme: {
    extend: {
      colors: {
        pve: {
          bg: '#1b1b1b',
          panel: '#2b2b2b',
          header: '#3d3d3d',
          border: '#4a4a4a',
          accent: '#0069a8',
          accent2: '#d9822b',
          text: '#d4d4d4',
          muted: '#8a8a8a',
          ok: '#6ab04c',
          warn: '#f9ca24',
          err: '#eb4d4b',
        },
      },
      fontFamily: {
        mono: ['"JetBrains Mono"', '"Consolas"', 'monospace'],
        ui: ['"Segoe UI"', 'system-ui', 'sans-serif'],
      },
    },
  },
  plugins: [],
}
