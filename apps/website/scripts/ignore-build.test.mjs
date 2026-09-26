import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { shouldSkip } from "./ignore-build.mjs";

test("deployment selection uses the last deployed revision and fails open", () => {
  const cwd = mkdtempSync(path.join(tmpdir(), "evolve-deploy-test-"));
  const git = (...args) =>
    execFileSync("git", args, {
      cwd,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
    }).trim();
  const write = (file, text) => {
    mkdirSync(path.dirname(path.join(cwd, file)), { recursive: true });
    writeFileSync(path.join(cwd, file), text);
  };
  const commit = () => {
    git("add", ".");
    git(
      "-c",
      "user.name=Evolve test",
      "-c",
      "user.email=test@example.invalid",
      "-c",
      "commit.gpgsign=false",
      "commit",
      "-m",
      "fixture",
    );
    return git("rev-parse", "HEAD");
  };
  try {
    git("init");
    write("apps/website/page.tsx", "first");
    const previous = commit();
    write("apps/simulation/main.cpp", "changed");
    commit();
    assert.equal(shouldSkip({ cwd, previous }), true, "C++-only change skips");
    write("docs/roadmap.md", "changed");
    commit();
    assert.equal(
      shouldSkip({ cwd, previous }),
      true,
      "documentation-only change skips",
    );
    write("apps/website/page.tsx", "second");
    commit();
    assert.equal(shouldSkip({ cwd, previous }), false, "website change builds");
    write("apps/simulation/main.cpp", "again");
    const latest = commit();
    assert.equal(
      shouldSkip({ cwd, previous }),
      false,
      "website change across multiple commits still builds",
    );
    write(".github/workflows/website.yml", "changed");
    commit();
    assert.equal(
      shouldSkip({ cwd, previous: latest }),
      false,
      "website workflow change builds",
    );
    assert.equal(
      shouldSkip({ cwd, previous: "" }),
      false,
      "first deployment builds",
    );
    assert.equal(
      shouldSkip({ cwd, previous: "missing-revision" }),
      false,
      "missing history builds",
    );
  } finally {
    rmSync(cwd, { recursive: true, force: true });
  }
});
