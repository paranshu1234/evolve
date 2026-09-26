# Evolve website

A public Next.js showcase for Evolve. It links to the independent C++ desktop project; it does not invoke the simulation or read Drive files.

## Develop and check

Requires Node.js 22+ and npm. From this directory:

```sh
npm ci
npm run dev
npm run typecheck
npm test
npm run build
npm start
```

Public copy and the repository URL are maintained in `content/project.ts`. Page structure and styles live under `src/app`; reusable illustration components live under `src/components`. `public` contains only publishable assets. The helix illustration is original SVG geometry, not a scientific output or desktop screenshot.

## Vercel deployment

1. Import the existing GitHub repository into the intended Vercel account/team.
2. Set **Root Directory** to `apps/website`, framework to **Next.js**, Node.js to **22.x**, and production branch to `main`.
3. Use `npm ci` to install and `npm run build` to build. No environment variables are required.
4. `vercel.json` configures `node scripts/ignore-build.mjs` as the Ignored Build Step. If the dashboard overrides it, set the same command there.
5. Verify a preview deployment before merging. Add the confirmed production URL to the repository overview after publication.

The ignore script compares the current revision with `VERCEL_GIT_PREVIOUS_SHA`. Simulation-only and unrelated documentation changes skip deployment. Website or website-workflow changes build. First deployments, unavailable history, and Git errors build rather than silently skipping. The tests exercise these cases across multiple commits.

Website checks in GitHub Actions run only for website/workflow changes. This is separate from Vercel deployment selection.

## Content publication

The site uses existing public project concepts. No private financial plans, pitch decks, research drafts, contact details, or Drive links are included. Review publication editions before adding downloadable documents. Repository links use the verified `paranshu1234/evolve` slug and are maintained in `content/project.ts`.
