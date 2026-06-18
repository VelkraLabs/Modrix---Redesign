/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Editor de Efeitos de status (MobEffect) + preview do frasco de
   poção com líquido tingido pela cor do efeito + ícone no HUD.
   ───────────────────────────────────────────────────────────────────────── */

const EFFECT_BEHAVIORS = [
  { id: 'regen',      label: 'Regeneração',  good: true },
  { id: 'strength',   label: 'Força',        good: true },
  { id: 'speed',      label: 'Velocidade',   good: true },
  { id: 'resistance', label: 'Resistência',  good: true },
  { id: 'glow',       label: 'Brilho',       good: true },
  { id: 'poison',     label: 'Veneno',       good: false },
  { id: 'weakness',   label: 'Fraqueza',     good: false },
  { id: 'slowness',   label: 'Lentidão',     good: false },
];
const EFFECT_COLORS = ['#e23b5b', '#f5b25c', '#5ce0a3', '#5fa9ff', '#c084fc', '#2dd4bf', '#ff7a8a', '#ffffff'];
const ROMAN = ['I', 'II', 'III', 'IV', 'V'];

// frasco que tinge o líquido ('L'/'d') com a cor do efeito direto no canvas
function PotionGlass({ color, size }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const rows = window.MX_SPRITES.pocao; const cv = ref.current; if (!cv || !rows) return;
    cv.width = 16; cv.height = 16;
    const ctx = cv.getContext('2d'); ctx.clearRect(0, 0, 16, 16);
    // escurece a cor pro fundo do líquido
    const dark = (() => {
      const h = color.replace('#', ''); const n = parseInt(h.length === 3 ? h.replace(/(.)/g, '$1$1') : h, 16);
      const r = Math.round(((n >> 16) & 255) * 0.6), g = Math.round(((n >> 8) & 255) * 0.6), b = Math.round((n & 255) * 0.6);
      return `rgb(${r},${g},${b})`;
    })();
    const pal = { '.': null, 'g': '#7a5536', 'w': '#cfe8f0', 'L': color, 'd': dark };
    for (let y = 0; y < 16; y++) for (let x = 0; x < (rows[y] || '').length; x++) {
      const col = pal[rows[y][x]]; if (col) { ctx.fillStyle = col; ctx.fillRect(x, y, 1, 1); }
    }
  }, [color]);
  return <canvas ref={ref} style={{ width: size, height: size, imageRendering: 'pixelated', display: 'block' }} />;
}

// ═══ EDITOR ════════════════════════════════════════════════════════════════
function EffectEditor({ rec, modId, set }) {
  return (
    <div>
      <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start' }}>
        <div>
          <label className="mx-label">Frasco</label>
          <div className="mx-tex-pick filled" style={{ cursor: 'default' }}>
            <span className="mx-potion-mini" style={{ '--liq': rec.color }}>
              <PotionGlass color={rec.color} size={50} />
            </span>
          </div>
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ marginBottom: 18 }}>
            <window.TextField label="Nome do efeito" value={rec.name}
              onChange={v => set({ name: v, id: rec._idEdited ? rec.id : window.toId(v) })}
              placeholder="Brilho Rúbeo" hint="Mostrado no inventário e no HUD." />
          </div>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: window.toId(v), _idEdited: true })} hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      <div className="mx-eff-section">Comportamento</div>
      <window.Segmented value={rec.category}
        options={[{ id: 'beneficial', label: 'Benéfico' }, { id: 'harmful', label: 'Prejudicial' }, { id: 'neutral', label: 'Neutro' }]}
        onChange={v => set({ category: v })} />
      <div className="mx-row" style={{ marginTop: 16 }}>
        <window.SelectField label="O que faz a cada tick" value={rec.behavior}
          options={EFFECT_BEHAVIORS.map(b => ({ id: b.id, label: b.label }))}
          onChange={v => set({ behavior: v })} hint="A mecânica aplicada enquanto ativo." />
        <div>
          <label className="mx-label">Cor do efeito</label>
          <div className="mx-swatch-row">
            {EFFECT_COLORS.map(c => (
              <button type="button" key={c} className={`mx-swatch${rec.color === c ? ' on' : ''}`}
                style={{ background: c }} onClick={() => set({ color: c })} title={c} />
            ))}
          </div>
        </div>
      </div>

      <div className="mx-eff-section">Potência</div>
      <div className="mx-row">
        <div>
          <label className="mx-label">Nível</label>
          <window.Segmented value={String(rec.amplifier)}
            options={ROMAN.map((r, i) => ({ id: String(i), label: r }))}
            onChange={v => set({ amplifier: Number(v) })} />
        </div>
        <window.NumberField label="Duração (segundos)" value={rec.duration} min={1} max={600}
          onChange={v => set({ duration: v })} hint="Tempo padrão ao aplicar." />
      </div>

      <div className="mx-eff-section">Exibição</div>
      <div className="mx-box" style={{ display: 'flex', flexDirection: 'column', gap: 13 }}>
        <window.CheckField label="Mostrar partículas" hint="Nuvem de partículas na cor do efeito."
          checked={rec.particles} onChange={v => set({ particles: v })} />
        <window.CheckField label="Mostrar ícone no HUD" hint="Aparece no canto superior direito."
          checked={rec.icon} onChange={v => set({ icon: v })} />
      </div>
      <div style={{ height: 16 }} />
    </div>
  );
}

// ═══ PREVIEW ═══════════════════════════════════════════════════════════════
function EffectPreview({ rec, modId, popKey }) {
  const beh = EFFECT_BEHAVIORS.find(b => b.id === rec.behavior) || {};
  const lvl = ROMAN[rec.amplifier] || 'I';
  const mins = Math.floor(rec.duration / 60), secs = rec.duration % 60;
  const time = `${mins}:${String(secs).padStart(2, '0')}`;
  const catLabel = rec.category === 'harmful' ? 'Prejudicial' : rec.category === 'neutral' ? 'Neutro' : 'Benéfico';

  return (
    <aside className="mx-preview">
      <div className="mx-pv-head">
        <span className="mx-pv-title"><span className="mx-live-dot" /> Preview ao vivo</span>
        <span style={{ fontSize: 11, color: 'var(--text-dim)', fontFamily: 'var(--font-mono, monospace)' }}>efeito</span>
      </div>
      <div className="mx-pv-body">
        <div className="mx-stage">
          <div key={popKey} className="mx-pop mx-potion-stage">
            {/* frasco grande tingido */}
            <span className="mx-potion-big" style={{ '--liq': rec.color }}>
              <PotionGlass color={rec.color} size={96} />
              {rec.particles && (
                <span className="mx-potion-parts">
                  {[0, 1, 2, 3, 4, 5].map(i => (
                    <i key={i} style={{ background: rec.color, '--i': i }} />
                  ))}
                </span>
              )}
            </span>
          </div>
        </div>

        {/* card de efeito no estilo do HUD */}
        <div className="mx-eff-hud">
          {rec.icon && <span className="mx-eff-hud-ico" style={{ '--liq': rec.color }}><PotionGlass color={rec.color} size={26} /></span>}
          <div style={{ flex: 1, minWidth: 0 }}>
            <div className="mx-eff-hud-name">{rec.name || 'Efeito'} {lvl}</div>
            <div className="mx-eff-hud-time">{time}</div>
          </div>
        </div>

        <div className="mx-pv-meta">
          <div className="mx-meta-card">
            <div className="mx-meta-row"><span className="mx-meta-k">Tipo</span>
              <span className="mx-meta-v" style={{ color: rec.category === 'harmful' ? 'var(--red)' : 'var(--accent)' }}>{catLabel}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Efeito</span>
              <span className="mx-meta-v">{beh.label} {lvl}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Duração</span>
              <span className="mx-meta-v">{rec.duration}s ({time})</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Exibição</span>
              <span className="mx-meta-v">{[rec.particles && 'partículas', rec.icon && 'ícone'].filter(Boolean).join(' · ') || 'oculto'}</span></div>
          </div>
        </div>
      </div>
    </aside>
  );
}

Object.assign(window, { EffectEditor, EffectPreview });
