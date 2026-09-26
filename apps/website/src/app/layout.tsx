import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Evolve — Exploring biological possibility",
  description:
    "An open project exploring biological autonomy through research, responsible design, and a local DNA visualization prototype.",
  icons: { icon: "/icon.svg" },
};

export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
