import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/run": "http://127.0.0.1:4000",
      "/example": "http://127.0.0.1:4000",
      "/generate": "http://127.0.0.1:4000"
    }
  }
});

