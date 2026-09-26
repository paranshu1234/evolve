import Helix from "../components/Helix";
import { pillars, repository, roadmap } from "../../content/project";

export default function Home() {
  return (
    <>
      <a className="skip-link" href="#main">
        Skip to content
      </a>
      <header className="header wrap">
        <a className="wordmark" href="#" aria-label="Evolve home">
          <span className="brand-mark">✳</span> evolve
          <span className="brand-dot">.</span>
        </a>
        <nav aria-label="Main navigation">
          <a href="#vision">The vision</a>
          <a href="#research">Research</a>
          <a href="#prototype">Prototype</a>
          <a href="#roadmap">Roadmap</a>
        </nav>
        <a className="header-link" href={repository}>
          Explore the project <span aria-hidden="true">↗</span>
        </a>
      </header>
      <main id="main">
        <section className="hero wrap" aria-labelledby="hero-title">
          <div className="hero-copy">
            <p className="eyebrow">
              <span className="live-dot" /> AN OPEN EXPLORATION IN BIOSCIENCE
            </p>
            <h1 id="hero-title">
              Biology is our origin.
              <br />
              <em>
                Possibility is
                <br />
                our direction.
              </em>
            </h1>
            <p className="lede">
              Exploring a future where people have greater agency over their
              living form and function. Grounded in curiosity. Guided by
              evidence.
            </p>
            <div className="actions">
              <a className="button" href="#vision">
                Discover Evolve <span aria-hidden="true">↗</span>
              </a>
              <a className="text-link" href="#prototype">
                Meet the prototype <span aria-hidden="true">→</span>
              </a>
            </div>
            <p className="hero-note">
              Early-stage research concept · Open development
            </p>
          </div>
          <div className="hero-art">
            <span className="art-label">FIG. 01 / THE LANGUAGE OF LIFE</span>
            <div className="orbit orbit-one" />
            <div className="orbit orbit-two" />
            <Helix />
            <div className="art-caption">
              <span>AT · GC · TA · CG</span>
              <span>SCHEMATIC STUDY — 001</span>
            </div>
          </div>
        </section>
        <div className="principles">
          <div className="wrap">
            <span>Individual choice</span>
            <span>Scientific evidence</span>
            <span>Responsible exploration</span>
            <span>Open development</span>
          </div>
        </div>
        <section id="vision" className="section wrap">
          <div className="section-heading">
            <p className="eyebrow">01 / THE VISION</p>
            <h2>
              From inherited limits
              <br />
              to <em>new questions.</em>
            </h2>
          </div>
          <p className="section-intro">
            What could biological autonomy mean for consenting adults? Evolve is
            a framework for investigating that question across three ambitions.
          </p>
          <div className="pillars">
            {pillars.map((p) => (
              <article key={p.number}>
                <span className="number">{p.number}</span>
                <h3>{p.title}</h3>
                <p>{p.text}</p>
              </article>
            ))}
          </div>
        </section>
        <section id="research" className="research">
          <div className="wrap research-grid">
            <div>
              <p className="eyebrow">02 / RESEARCH & RESPONSIBILITY</p>
              <h2>
                Big questions.
                <br />
                <em>Careful next steps.</em>
              </h2>
            </div>
            <div>
              <p className="research-lede">
                A compelling vision is a starting point. Evidence determines
                what comes next.
              </p>
              <p>
                We are defining the scientific questions, technical boundaries,
                and ethical conditions that meaningful progress would require.
                Feasibility, safety, and reversibility remain questions to
                investigate.
              </p>
              <a
                className="text-link"
                href={`${repository}/tree/main/docs/research`}
              >
                Read the research scope <span aria-hidden="true">↗</span>
              </a>
              <div className="research-note">
                The project is at a foundational stage. It offers no treatment,
                medical advice, or prediction of biological outcomes.
              </div>
            </div>
          </div>
        </section>
        <section id="prototype" className="section wrap prototype-grid">
          <div className="prototype-art">
            <div className="window-bar">
              <span>EVOLVE / GENOME WORKSPACE</span>
              <span>v0.1</span>
            </div>
            <Helix compact />
            <div className="sequence-strip">A T G C G T A C C T A G</div>
            <p>Illustration of the desktop concept</p>
          </div>
          <div>
            <p className="eyebrow">03 / THE FIRST PROTOTYPE</p>
            <h2>
              A small window
              <br />
              into <em>complexity.</em>
            </h2>
            <p className="section-intro">
              Explore a schematic DNA helix in a local Windows workspace. Select
              a base, try a virtual substitution, and compare it with the
              original sequence.
            </p>
            <ul className="feature-list">
              <li>Interactive 3D visualization</li>
              <li>Reversible edits and baseline comparison</li>
              <li>Local project files and composition statistics</li>
            </ul>
            <a
              className="button"
              href={`${repository}/tree/main/apps/simulation`}
            >
              Explore the desktop prototype <span aria-hidden="true">↗</span>
            </a>
            <p className="small-note">
              Windows / C++ / DirectX 12. Composition analysis is a mock
              demonstration; scientific inference is not connected.
            </p>
          </div>
        </section>
        <section id="roadmap" className="section wrap roadmap">
          <div className="section-heading">
            <p className="eyebrow">04 / THE PATH AHEAD</p>
            <h2>
              Built one question
              <br />
              <em>at a time.</em>
            </h2>
          </div>
          <div className="roadmap-grid">
            {roadmap.map((item) => (
              <article key={item.stage}>
                <p className="eyebrow">{item.stage}</p>
                <span className="status">{item.status}</span>
                <h3>{item.title}</h3>
                <p>{item.text}</p>
              </article>
            ))}
          </div>
        </section>
        <section className="join wrap">
          <p className="eyebrow">CURIOSITY IS A GOOD PLACE TO START</p>
          <h2>
            Help shape
            <br />
            <em>the questions ahead.</em>
          </h2>
          <p>
            Bring your perspective to the research, software, or design. Follow
            the work and start a conversation on GitHub.
          </p>
          <a className="button" href={`${repository}/issues`}>
            Join the conversation <span aria-hidden="true">↗</span>
          </a>
        </section>
      </main>
      <footer className="wrap footer">
        <a className="wordmark" href="#">
          evolve.
        </a>
        <p>Exploring biological possibility, responsibly.</p>
        <a href={repository}>GitHub ↗</a>
        <span>© {new Date().getFullYear()} Project Evolve</span>
      </footer>
    </>
  );
}
