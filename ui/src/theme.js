import { createTheme } from '@mui/material/styles';

export default createTheme({
  palette: {
    mode: 'dark',
    primary: { main: '#f2675f', contrastText: '#170b0b' },
    background: { default: '#0c0f14', paper: '#151922' },
    text: { primary: '#f5f2ed', secondary: '#98a2b3' },
    divider: 'rgba(199, 211, 229, 0.10)',
    success: { main: '#72b99a' },
    warning: { main: '#e3ad65' },
    error: { main: '#f2675f' },
  },
  typography: {
    fontFamily: 'Manrope Variable, Segoe UI, sans-serif',
    h1: { fontFamily: 'Space Grotesk Variable, sans-serif', fontWeight: 620, letterSpacing: '-0.045em' },
    h2: { fontFamily: 'Space Grotesk Variable, sans-serif', fontWeight: 620, letterSpacing: '-0.03em' },
    button: { fontWeight: 720, letterSpacing: '-0.01em', textTransform: 'none' },
  },
  shape: { borderRadius: 14 },
  components: {
    MuiButton: {
      styleOverrides: {
        root: {
          minHeight: 44,
          borderRadius: 12,
          boxShadow: 'none',
          transition: 'transform 160ms ease, background-color 180ms ease, border-color 180ms ease',
          '&:active': { transform: 'translateY(1px) scale(0.99)' },
        },
      },
    },
    MuiSwitch: {
      styleOverrides: {
        root: { padding: 8 },
        switchBase: { '&.Mui-checked + .MuiSwitch-track': { opacity: 0.55 } },
      },
    },
    MuiTooltip: {
      styleOverrides: {
        tooltip: { background: '#252b35', fontSize: 12, border: '1px solid rgba(255,255,255,.08)' },
      },
    },
  },
});
