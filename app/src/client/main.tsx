import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import ClientApp from "./ClientApp";
import "../styles.css";
import "./client.css";

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <ClientApp />
  </StrictMode>,
);
