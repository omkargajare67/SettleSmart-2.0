/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        fintech: {
          dark: '#080C14',
          navy: '#0B0F19',
          surface: '#111827',
          card: '#161F30',
          elevated: '#1E293B',
          border: '#233248',
          borderSubtle: '#1B2637',
          emerald: '#10B981',
          emeraldHover: '#059669',
          emeraldDim: 'rgba(16, 185, 129, 0.12)',
          teal: '#14B8A6',
          blue: '#3B82F6',
          amber: '#F59E0B',
          amberDim: 'rgba(245, 158, 11, 0.12)',
          red: '#EF4444',
          redDim: 'rgba(239, 68, 68, 0.12)',
          muted: '#94A3B8',
          text: '#F1F5F9',
        }
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', '-apple-system', 'BlinkMacSystemFont', 'Segoe UI', 'Roboto', 'sans-serif'],
        mono: ['JetBrains Mono', 'ui-monospace', 'SFMono-Regular', 'Menlo', 'Monaco', 'Consolas', 'monospace'],
      },
      boxShadow: {
        'fintech-card': '0 4px 20px -2px rgba(0, 0, 0, 0.45)',
        'fintech-glow': '0 0 25px -5px rgba(16, 185, 129, 0.25)',
      }
    },
  },
  plugins: [],
}
