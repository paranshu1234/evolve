# Contributing to Evolve

Use issues for reproducible bugs, research questions, and design proposals. Keep pull requests focused on one component or one documented cross-component change.

## Checks

- Website: in `apps/website`, run `npm ci`, `npm run typecheck`, `npm test`, and `npm run build`. Check desktop and mobile layouts, keyboard navigation, and reduced-motion preferences.
- Simulation: configure a clean build from `apps/simulation`, build, and run CTest. Windows graphics changes also require the documented WARP smoke test and manual viewport checks.
- Documentation: check relative links and distinguish existing behavior, hypotheses, and planned capabilities.

CI selects applications by changed paths. A simulation release is independent of the website and uses a `simulation-v*` tag. Vercel publishes the website from its own directory.

## Content boundaries

Do not commit credentials, personal data, internal business files, Drive document indexes, build output, dependencies, or ZIP backups. Keep private documents in their existing restricted storage. Publish only selected material that is ready to be public; record version and date for publication snapshots. Website copy belongs in `apps/website/content`; technical documentation belongs with its owning component or under `docs`.

Preserve the project's baseline/revision checks and clearly label mock composition results. Changes to scientific claims should include supporting references and limitations.
