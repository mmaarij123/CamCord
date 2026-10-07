import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import CssBaseline from '@mui/material/CssBaseline';
import { ThemeProvider } from '@mui/material/styles';
import '@fontsource-variable/manrope';
import '@fontsource-variable/space-grotesk';
import '@fontsource-variable/jetbrains-mono';
import App from './App.jsx';
import theme from './theme.js';
import './styles.css';

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <App />
    </ThemeProvider>
  </StrictMode>,
);
