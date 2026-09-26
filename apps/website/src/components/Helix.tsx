export default function Helix({ compact = false }: { compact?: boolean }) {
  const points = Array.from({ length: 38 }, (_, i) => {
    const phase = i * 0.32;
    return {
      y: 28 + i * 13,
      a: 210 + Math.sin(phase) * 125,
      b: 210 - Math.sin(phase) * 125,
      opacity: 0.35 + (Math.cos(phase) + 1) * 0.3,
    };
  });
  return (
    <svg
      className={compact ? "helix helix-small" : "helix"}
      viewBox="0 0 420 550"
      role="img"
      aria-label="Schematic double helix illustration"
    >
      <defs>
        <linearGradient id={compact ? "strand-small" : "strand"}>
          <stop stopColor="#759166" />
          <stop offset="1" stopColor="#d3e6a0" />
        </linearGradient>
      </defs>
      <path
        d={points.map((p, i) => `${i ? "L" : "M"}${p.a},${p.y}`).join(" ")}
        fill="none"
        stroke="#a6ba82"
        strokeWidth="2"
      />
      <path
        d={points.map((p, i) => `${i ? "L" : "M"}${p.b},${p.y}`).join(" ")}
        fill="none"
        stroke="#708260"
        strokeWidth="2"
      />
      {points.map((p, i) => (
        <g key={i} opacity={p.opacity}>
          <line
            x1={p.a}
            y1={p.y}
            x2={p.b}
            y2={p.y}
            stroke={`url(#${compact ? "strand-small" : "strand"})`}
            strokeWidth="3"
          />
          <circle cx={p.a} cy={p.y} r="5" fill="#d4e6ac" />
          <circle cx={p.b} cy={p.y} r="5" fill="#93aa73" />
        </g>
      ))}
    </svg>
  );
}
