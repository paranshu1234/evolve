import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

// Vercel: 0 skips deployment; 1 builds. Unknown history must build.
export function shouldSkip({
  cwd = process.cwd(),
  previous = process.env.VERCEL_GIT_PREVIOUS_SHA,
  current = process.env.VERCEL_GIT_COMMIT_SHA || "HEAD",
} = {}) {
  if (!previous) return false;
  try {
    const root = execFileSync("git", ["rev-parse", "--show-toplevel"], {
      cwd,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
    }).trim();
    const changed = execFileSync(
      "git",
      [
        "diff",
        "--name-only",
        previous,
        current,
        "--",
        "apps/website",
        ".github/workflows/website.yml",
      ],
      { cwd: root, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] },
    );
    return changed.trim() === "";
  } catch {
    return false;
  }
}

if (
  process.argv[1] &&
  path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  const skip = shouldSkip();
  console.log(
    skip
      ? "Skip: website unchanged since previous deployment."
      : "Build: website changed or comparison unavailable.",
  );
  process.exit(skip ? 0 : 1);
}
