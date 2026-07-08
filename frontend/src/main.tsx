import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { applyTheme } from "./styles/theme";
import "./index.css";
import App from "./App.tsx";

applyTheme();

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
