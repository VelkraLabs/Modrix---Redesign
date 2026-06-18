/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Editor de Minérios (geração no mundo) + preview "corte do
   subsolo": coluna estratificada com eixo Y, faixa de geração destacada
   e pontos de minério com densidade ∝ tentativas/chunk e veia.
   ───────────────────────────────────────────────────────────────────────── */

const ORE_DIMENSIONS = [
  { id: 'overworld', label: 'Overworld' },
  { id: 'nether',    label: 'Nether' },
  { id: 'end',       label: 'End' },
];

// ═══ EDITOR ════════════════════════════════════════════════════════════════
function OreEditor({ rec, modId, set }) {
  const blocks = (window.MX_MOD && window.MX_MOD.blocks) || [];
  const blockOpts = blocks.length
    ? blocks.map(b => ({ id: b.id, label: b.name }))
    : [{ id: rec.block || '', label: rec.block || '—' }];

  return (
    <div>
      <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start' }}>
        <div>
          <label className="mx-label">Bloco</label>
          <div className="mx-tex-pick filled" style={{ cursor: 'default' }}>
            <window.PixelSprite sprite={rec.sprite || 'ore'} size={58} />
          </div>
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ marginBottom: 18 }}>
            <window.TextField label="Nome da geração" value={rec.name}
              onChange={v => set({ name: v, id: rec._idEdited ? rec.id : window.toId(v) })}
              placeholder="Veio de Rubi" />
          </div>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: window.toId(v), _idEdited: true })} hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      <div style={{ fontSize: 12, fontWeight: 800, letterSpacing: '.8px', textTransform: 'uppercase',
        color: 'var(--text-dim)', margin: '24px 0 12px', paddingBottom: 8, borderBottom: '1px solid var(--border-sub)' }}>
        Geração
      </div>
      <div className="mx-row">
        <window.SelectField label="Bloco gerado" value={rec.block} options={blockOpts}
          onChange={v => { const b = blocks.find(x => x.id === v); set({ block: v, sprite: b ? b.sprite : rec.sprite }); }}
          hint="Um bloco do seu mod." />
        <div>
          <label className="mx-label">Dimensão</label>
          <window.Segmented value={rec.dimension} options={ORE_DIMENSIONS} onChange={v => set({ dimension: v })} />
        </div>
      </div>
      <div className="mx-row" style={{ marginTop: 16 }}>
        <window.NumberField label="Tamanho da veia" value={rec.veinSize} min={1} max={24}
          onChange={v => set({ veinSize: v })} hint="Blocos por veio. Ferro = 9 · Diamante = 8." />
        <window.NumberField label="Veios por chunk" value={rec.perChunk} min={1} max={32}
          onChange={v => set({ perChunk: v })} hint="Tentativas de geração por chunk 16×16." />
      </div>
      <div className="mx-row" style={{ marginTop: 16 }}>
        <window.NumberField label="Altura mínima (Y)" value={rec.minY} min={-64} max={320}
          onChange={v => set({ minY: Math.min(v, rec.maxY - 1) })} hint="-64 = fundo do mundo." />
        <window.NumberField label="Altura máxima (Y)" value={rec.maxY} min={-63} max={320}
          onChange={v => set({ maxY: Math.max(v, rec.minY + 1) })} hint="Diamante gera até Y=16." />
      </div>
      {rec.dimension === 'overworld' && (
        <div className="mx-box" style={{ marginTop: 18 }}>
          <window.CheckField label="Variante deepslate" hint="Abaixo de Y=0 usa a versão em ardósia."
            checked={!!rec.deepslate} onChange={v => set({ deepslate: v })} />
        </div>
      )}
      <div style={{ height: 16 }} />
    </div>
  );
}

// ═══ PREVIEW: corte do subsolo ══════════════════════════════════════════════
// Coluna de Y=104 a Y=-64; estratos por dimensão; pontos pseudo-aleatórios
// (semente estável por id) dentro da faixa [minY, maxY].
function oreSeedRand(seed) {
  let s = 0;
  for (let i = 0; i < seed.length; i++) s = (s * 31 + seed.charCodeAt(i)) >>> 0;
  return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; };
}

function OrePreview({ rec, modId, popKey }) {
  const TOP = 104, BOTTOM = -64, H = 240; // px do corte
  const yToPx = y => ((TOP - y) / (TOP - BOTTOM)) * H;
  const minY = Math.max(BOTTOM, Math.min(rec.minY, rec.maxY));
  const maxY = Math.min(TOP, Math.max(rec.minY, rec.maxY));

  // estratos por dimensão
  const strata = rec.dimension === 'nether'
    ? [{ to: TOP, c: '#3a1318' }, { to: 30, c: '#541c22' }, { to: BOTTOM, c: '#451217' }]
    : rec.dimension === 'end'
      ? [{ to: TOP, c: '#2a2438' }, { to: BOTTOM, c: '#3a3450' }]
      : [{ to: 70, c: '#4a8a3a' }, { to: 60, c: '#6e4a26' }, { to: 0, c: '#5b626e' }, { to: -60, c: '#3a3f47' }, { to: BOTTOM, c: '#23262c' }];

  // pontos de minério (densidade ∝ perChunk, tamanho ∝ veinSize)
  const rand = oreSeedRand(rec.id + rec.dimension);
  const count = Math.min(48, (rec.perChunk || 1) * 3);
  const dotSize = 3 + Math.min(7, (rec.veinSize || 1) * 0.45);
  const dots = Array.from({ length: count }, (_, i) => {
    const y = minY + rand() * (maxY - minY);
    return { id: i, left: 8 + rand() * 84, top: yToPx(y), s: dotSize * (0.7 + rand() * 0.6) };
  });

  const marks = [80, 40, 0, -40].filter(y => y > BOTTOM && y < TOP);

  return (
    <aside className="mx-preview">
      <div className="mx-pv-head">
        <span className="mx-pv-title"><span className="mx-live-dot" /> Preview ao vivo</span>
        <span style={{ fontSize: 11, color: 'var(--text-dim)', fontFamily: 'var(--font-mono, monospace)' }}>geração</span>
      </div>
      <div className="mx-pv-body">
        <div className="mx-stage" style={{ paddingTop: 14, paddingBottom: 14 }}>
          <div key={popKey} className="mx-pop mx-cut-wrap">
            {/* eixo Y */}
            <div className="mx-cut-axis" style={{ height: H }}>
              {[TOP, ...marks, BOTTOM].map(y => (
                <span key={y} className="mx-cut-mark" style={{ top: yToPx(y) }}>{y}</span>
              ))}
            </div>
            {/* coluna do mundo */}
            <div className="mx-cut-col" style={{ height: H }}>
              {strata.map((st, i) => {
                const from = i === 0 ? TOP : strata[i - 1].to;
                return <div key={i} className="mx-cut-stratum" style={{ top: yToPx(from), height: yToPx(st.to) - yToPx(from), background: st.c }} />;
              })}
              {/* faixa de geração */}
              <div className="mx-cut-band" style={{ top: yToPx(maxY), height: Math.max(4, yToPx(minY) - yToPx(maxY)) }} />
              {dots.map(d => (
                <span key={d.id} className="mx-cut-dot" style={{ top: d.top, left: d.left + '%', width: d.s, height: d.s }} />
              ))}
              {/* bedrock */}
              <div className="mx-cut-bedrock" />
            </div>
          </div>
        </div>

        <div className="mx-pv-meta">
          <div className="mx-meta-card">
            <div className="mx-meta-row"><span className="mx-meta-k">Faixa</span>
              <span className="mx-meta-v" style={{ color: 'var(--accent)' }}>Y {minY} → {maxY}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Densidade</span>
              <span className="mx-meta-v">{rec.perChunk} veios/chunk · {rec.veinSize} blocos/veio</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Dimensão</span>
              <span className="mx-meta-v">{(ORE_DIMENSIONS.find(d => d.id === rec.dimension) || {}).label}{rec.dimension === 'overworld' && rec.deepslate ? ' · deepslate' : ''}</span></div>
          </div>
          <div style={{ fontSize: 11, color: 'var(--text-dim)', textAlign: 'center', lineHeight: 1.5 }}>
            Corte de um chunk — cada ponto é um veio possível. Ajuste a faixa Y e veja a banda se mover.
          </div>
        </div>
      </div>
    </aside>
  );
}

Object.assign(window, { OreEditor, OrePreview });
