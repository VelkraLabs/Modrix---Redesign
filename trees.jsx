/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Editor de Árvores (geração de vegetação) + preview de elevação:
   tronco + copa montados com as texturas de folhas/tronco, em altura real.
   ───────────────────────────────────────────────────────────────────────── */

const TREE_SHAPES = [
  { id: 'blob',   label: 'Copa redonda' },
  { id: 'pine',   label: 'Pinheiro' },
  { id: 'bush',   label: 'Arbusto' },
];
const TREE_DIMS = [
  { id: 'overworld', label: 'Overworld' },
  { id: 'nether',    label: 'Nether' },
  { id: 'end',       label: 'End' },
];

// ═══ EDITOR ════════════════════════════════════════════════════════════════
function TreeEditor({ rec, modId, set, onEditTexture }) {
  return (
    <div>
      <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start' }}>
        <div>
          <label className="mx-label">Folhas</label>
          <div className="mx-tex-pick filled" style={{ cursor: 'default' }}>
            <window.PixelSprite sprite={rec.leaves || 'folhas'} size={58} />
            <button type="button" className="mx-tex-edit" title="Pintar folhas"
              onClick={() => onEditTexture && onEditTexture(rec.leaves || 'folhas')}>
              <window.Icon name="pen" style="line" size={13} />
            </button>
          </div>
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ marginBottom: 18 }}>
            <window.TextField label="Nome da árvore" value={rec.name}
              onChange={v => set({ name: v, id: rec._idEdited ? rec.id : window.toId(v) })}
              placeholder="Árvore Rúbea" />
          </div>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: window.toId(v), _idEdited: true })} hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      <div className="mx-eff-section">Forma</div>
      <window.Segmented value={rec.shape} options={TREE_SHAPES} onChange={v => set({ shape: v })} />

      <div className="mx-eff-section">Tamanho</div>
      <div className="mx-row">
        <window.NumberField label="Altura mínima" value={rec.minHeight} min={1} max={30}
          onChange={v => set({ minHeight: Math.min(v, rec.maxHeight) })} hint="Em blocos de tronco." />
        <window.NumberField label="Altura máxima" value={rec.maxHeight} min={1} max={30}
          onChange={v => set({ maxHeight: Math.max(v, rec.minHeight) })} hint="Variação por árvore." />
      </div>

      <div className="mx-eff-section">Geração</div>
      <div className="mx-row">
        <div>
          <label className="mx-label">Dimensão</label>
          <window.Segmented value={rec.dimension} options={TREE_DIMS} onChange={v => set({ dimension: v })} />
        </div>
        <window.NumberField label="Árvores por chunk" value={rec.perChunk} min={0} max={20}
          onChange={v => set({ perChunk: v })} hint="Densidade no bioma. 0 = só plantável." />
      </div>
      <div style={{ height: 16 }} />
    </div>
  );
}

// ═══ PREVIEW: elevação da árvore ════════════════════════════════════════════
function TreePreview({ rec, modId, popKey }) {
  const h = Math.round((rec.minHeight + rec.maxHeight) / 2);
  const CELL = 15;
  const trunkH = h;
  const shape = rec.shape;

  // monta a copa por forma (grade de células de folha)
  let canopy = [];
  if (shape === 'pine') {
    // triângulo: linhas crescendo
    const rows = 5;
    for (let r = 0; r < rows; r++) {
      const w = 1 + r;
      canopy.push({ y: r, x0: -w, x1: w });
    }
  } else if (shape === 'bush') {
    canopy = [{ y: 0, x0: -2, x1: 2 }, { y: 1, x0: -2, x1: 2 }];
  } else {
    // blob: redondo
    canopy = [
      { y: 0, x0: -1, x1: 1 }, { y: 1, x0: -2, x1: 2 },
      { y: 2, x0: -2, x1: 2 }, { y: 3, x0: -1, x1: 1 },
    ];
  }
  const canopyRows = canopy.length;
  const leafCells = [];
  canopy.forEach(row => {
    for (let x = row.x0; x <= row.x1; x++) leafCells.push({ x, y: row.y });
  });
  const dimLabel = (TREE_DIMS.find(d => d.id === rec.dimension) || {}).label;

  return (
    <aside className="mx-preview">
      <div className="mx-pv-head">
        <span className="mx-pv-title"><span className="mx-live-dot" /> Preview ao vivo</span>
        <span style={{ fontSize: 11, color: 'var(--text-dim)', fontFamily: 'var(--font-mono, monospace)' }}>árvore</span>
      </div>
      <div className="mx-pv-body">
        <div className="mx-stage" style={{ paddingTop: 14, paddingBottom: 0 }}>
          <div key={popKey} className="mx-pop mx-tree-stage">
            {/* copa */}
            <div className="mx-tree-canopy" style={{ height: canopyRows * CELL, width: 5 * CELL }}>
              {leafCells.map((c, i) => (
                <span key={i} className="mx-tree-leaf"
                  style={{ left: (c.x + 2) * CELL, top: c.y * CELL, width: CELL, height: CELL }}>
                  <window.PixelSprite sprite={rec.leaves || 'folhas'} size={CELL} />
                </span>
              ))}
            </div>
            {/* tronco */}
            <div className="mx-tree-trunk" style={{ height: trunkH * CELL, width: CELL }}>
              {Array.from({ length: trunkH }, (_, i) => (
                <window.PixelSprite key={i} sprite={rec.trunk || 'tronco'} size={CELL} />
              ))}
            </div>
            {/* chão */}
            <div className="mx-tree-ground" />
          </div>
        </div>

        <div className="mx-pv-meta">
          <div className="mx-meta-card">
            <div className="mx-meta-row"><span className="mx-meta-k">Forma</span>
              <span className="mx-meta-v" style={{ color: 'var(--accent)' }}>{(TREE_SHAPES.find(s => s.id === shape) || {}).label}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Altura</span>
              <span className="mx-meta-v">{rec.minHeight}–{rec.maxHeight} blocos</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Geração</span>
              <span className="mx-meta-v">{dimLabel} · {rec.perChunk}/chunk</span></div>
          </div>
          <div style={{ fontSize: 11, color: 'var(--text-dim)', textAlign: 'center', lineHeight: 1.5 }}>
            Altura média mostrada ({h} blocos) — cada árvore varia entre o mínimo e o máximo.
          </div>
        </div>
      </div>
    </aside>
  );
}

Object.assign(window, { TreeEditor, TreePreview });
