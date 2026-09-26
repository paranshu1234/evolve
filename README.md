# Evolve

An open project exploring biological autonomy through research, responsible design, and software prototypes. The current desktop application visualizes short DNA sequences and reversible virtual edits. It does not predict biological outcomes.

## Explore

- [Website](apps/website/README.md): Next.js showcase, deployed independently on Vercel.
- [Desktop prototype](apps/simulation/README.md): C++17 / Windows / DirectX 12.
- [Vision](docs/concept/vision.md), [research scope](docs/research/README.md), and [requirements](docs/requirements/README.md).
- [Project architecture](docs/architecture/README.md) and [roadmap](docs/roadmap.md).
- [Contributing](CONTRIBUTING.md).

## Local development

Website (Node.js 22 or newer):

```sh
cd apps/website
npm ci
npm run dev
```

Simulation core (CMake 3.20+, C++17 compiler):

```sh
cmake -S apps/simulation -B apps/simulation/build
cmake --build apps/simulation/build --config Release
ctest --test-dir apps/simulation/build -C Release --output-on-failure
```

Windows also builds the desktop application. See its README for prerequisites and smoke testing. The two applications have independent dependencies, builds, tests, and releases; there is no root JavaScript workspace or simulation API.

## Publications and downloads

Desktop binaries belong in [GitHub Releases](https://github.com/paranshu1234/evolve/releases), created by `simulation-v*` tags after tests pass. A release may not yet be available; the desktop README includes source build instructions. The website's live URL will be added after its first verified deployment.

## Public and private materials

GitHub is authoritative for source code and maintained public technical documentation. Working research documents, financial plans, pitch decks, private document indexes, and original design explorations remain in restricted Drive storage. Only reviewed publication editions and selected website assets should be copied into this repository. No automatic Drive synchronization is configured.

The public repository is [paranshu1234/evolve](https://github.com/paranshu1234/evolve). It preserves the history of the original `evolve.ai` repository.
