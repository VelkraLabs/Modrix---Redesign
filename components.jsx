/* ─────────────────────────────────────────────────────────────────────────
   Modrix — componentes base
   PixelSprite (canvas), Icon (emoji ou linha), átomos de formulário, Segmented
   ───────────────────────────────────────────────────────────────────────── */
const { useRef, useEffect } = React;

// ── Sprite pixel-art: desenha um mapa de chars num canvas (nearest-neighbor) ──
// Respeita overrides do pixel editor (window.MX_TEX_OVERRIDES) e redesenha
// quando o evento 'mx-tex' anuncia que a textura mudou.
function PixelSprite({ sprite, palette, size = 64, glint = false }) {
  const ref = useRef(null);
  const [tv, setTv] = React.useState(0);
  useEffect(() => {
    const fn = e => { if (!e.detail || !e.detail.sprite || e.detail.sprite === sprite) setTv(v => v + 1); };
    window.addEventListener('mx-tex', fn);
    return () => window.removeEventListener('mx-tex', fn);
  }, [sprite]);
  useEffect(() => {
    const ov = (window.MX_TEX_OVERRIDES || {})[sprite];
    const rows = ov ? ov.rows : window.MX_SPRITES[sprite];
    const pal = ov ? ov.palette : window.MX_PALETTES[palette || sprite];
    const cv = ref.current; if (!cv || !rows) return;
    const n = rows.length;
    cv.width = n; cv.height = n;
    const ctx = cv.getContext('2d');
    ctx.clearRect(0, 0, n, n);
    for (let y = 0; y < n; y++) {
      for (let x = 0; x < rows[y].length; x++) {
        const col = pal[rows[y][x]];
        if (col) { ctx.fillStyle = col; ctx.fillRect(x, y, 1, 1); }
      }
    }
  }, [sprite, palette, tv]);
  return (
    <canvas
      ref={ref}
      className={glint ? 'mx-glint' : ''}
      style={{ width: size, height: size, imageRendering: 'pixelated',
        filter: 'drop-shadow(2px 3px 2px rgba(0,0,0,.45))' }}
    />
  );
}

// ── Ícones: alterna entre emoji e SVG de linha conforme tweak `iconStyle` ──
const EMOJI = {
  cube: '📦', brick: '🧱', pick: '⛏️', shield: '🛡️', grid: '🔲', spark: '✨',
  mob: '🐺', ore: '⛏️', tree: '🌳', run: '▶', history: '🕒', search: '🔍',
  code: '</>', console: '▸', folder: '📁', check: '✓', pen: '✏️', copy: '⧉', trash: '🗑️',
};
function LineIcon({ name, size }) {
  const p = { fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round' };
  const paths = {
    cube:  <g {...p}><path d="M12 3 21 7.5v9L12 21 3 16.5v-9z"/><path d="M3 7.5 12 12l9-4.5M12 12v9"/></g>,
    brick: <g {...p}><rect x="3.5" y="6" width="17" height="12" rx="1"/><path d="M3.5 12h17M12 6v6M8 12v6M16 12v6"/></g>,
    pick:  <g {...p}><path d="M4 20 14 10"/><path d="M5 9c4-4 10-4 14 0-3-1.5-5-1-7 1-2 2-2.5 4-1 7-4-4-4-8-6-8z"/></g>,
    shield:<g {...p}><path d="M12 3l7 2.5v5c0 5-3.2 8-7 10-3.8-2-7-5-7-10v-5z"/></g>,
    grid:  <g {...p}><rect x="4" y="4" width="16" height="16" rx="1.5"/><path d="M4 9.3h16M4 14.6h16M9.3 4v16M14.6 4v16"/></g>,
    spark: <g {...p}><path d="M12 3v4M12 17v4M3 12h4M17 12h4M6 6l2.5 2.5M15.5 15.5 18 18M18 6l-2.5 2.5M8.5 15.5 6 18"/></g>,
    mob:   <g {...p}><path d="M5 9c0-2 2-3 3.5-2.5L12 4l3.5 2.5C17 6 19 7 19 9v6l-2 3H7l-2-3z"/><path d="M9.5 11h.01M14.5 11h.01"/></g>,
    ore:   <g {...p}><path d="M12 3 21 8v8l-9 5-9-5V8z"/><path d="M10 9l2.5 2 2-1.5M9 14l2-1 2 2"/></g>,
    tree:  <g {...p}><path d="M12 21v-5M12 16c-3 0-5-2-5-4 0-1 .5-2 1.5-2.5C8 7 9.5 6 12 6s4 1 3.5 3.5C16.5 10 17 11 17 12c0 2-2 4-5 4z"/></g>,
    run:   <g {...p}><path d="M7 4.5 19 12 7 19.5z" fill="currentColor"/></g>,
    history:<g {...p}><path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1M3.5 5v4h4M12 8v4.5l3 1.8"/></g>,
    search:<g {...p}><circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5 21 21"/></g>,
    code:  <g {...p}><path d="M9 7l-5 5 5 5M15 7l5 5-5 5"/></g>,
    console:<g {...p}><rect x="3.5" y="5" width="17" height="14" rx="2"/><path d="M7 10l3 2.5-3 2.5M12.5 15.5h4"/></g>,
    folder:<g {...p}><path d="M3.5 7a1.5 1.5 0 0 1 1.5-1.5h4l2 2h7A1.5 1.5 0 0 1 20.5 9v8a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 17z"/></g>,
    check: <g {...p}><path d="M5 12.5 10 17 19 7"/></g>,
    pen:   <g {...p}><path d="M4 20l1-4L16.5 4.5a2.1 2.1 0 0 1 3 3L8 19z"/><path d="M13.5 7.5l3 3"/></g>,
    copy:  <g {...p}><rect x="8.5" y="8.5" width="12" height="12" rx="2"/><path d="M15.5 8.5V5.5a2 2 0 0 0-2-2h-8a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h3"/></g>,
    trash: <g {...p}><path d="M4.5 7h15M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13M10 11v5M14 11v5"/></g>,
  };
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" style={{ display: 'block' }}>
      {paths[name] || paths.cube}
    </svg>
  );
}
function Icon({ name, style = 'line', size = 16 }) {
  if (style === 'emoji') return <span style={{ fontSize: size * 0.92, lineHeight: 1 }}>{EMOJI[name] || '•'}</span>;
  return <LineIcon name={name} size={size} />;
}

// ── Átomos de formulário ──────────────────────────────────────────────────
function TextField({ label, value, onChange, placeholder, hint, mono, autoFocus }) {
  return (
    <div>
      <label className="mx-label">{label}</label>
      <input className="mx-field-input" value={value} placeholder={placeholder} autoFocus={autoFocus}
        style={mono ? { fontFamily: 'var(--font-mono, monospace)', fontSize: 13 } : null}
        onChange={e => onChange(e.target.value)} />
      {hint && <div className="mx-hint">{hint}</div>}
    </div>
  );
}
function NumberField({ label, value, onChange, hint, min, max, step }) {
  return (
    <div>
      <label className="mx-label">{label}</label>
      <input className="mx-field-input" type="number" value={value} min={min} max={max} step={step}
        onChange={e => onChange(e.target.value === '' ? '' : Number(e.target.value))} />
      {hint && <div className="mx-hint">{hint}</div>}
    </div>
  );
}
function SelectField({ label, value, onChange, options }) {
  return (
    <div>
      <label className="mx-label">{label}</label>
      <select className="mx-field-input" value={value} onChange={e => onChange(e.target.value)}>
        {options.map(o => <option key={o.id} value={o.id}>{o.label}</option>)}
      </select>
    </div>
  );
}
function CheckField({ label, hint, checked, onChange }) {
  return (
    <label className="mx-check">
      <input type="checkbox" checked={checked} onChange={e => onChange(e.target.checked)} />
      <span style={{ cursor: 'pointer' }}>{label}
        {hint && <span className="mx-hint" style={{ marginLeft: 8, display: 'inline', fontStyle: 'normal' }}>{hint}</span>}
      </span>
    </label>
  );
}
function Segmented({ options, value, onChange }) {
  return (
    <div className="mx-seg">
      {options.map(o => (
        <button key={o.id} type="button" className={value === o.id ? 'on' : ''} onClick={() => onChange(o.id)}>
          {o.label}
        </button>
      ))}
    </div>
  );
}

Object.assign(window, { PixelSprite, Icon, LineIcon, TextField, NumberField, SelectField, CheckField, Segmented });
