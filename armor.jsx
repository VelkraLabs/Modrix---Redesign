/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Editor de Armaduras (conjunto por material, como no app real:
   define o material e quais peças gerar) + preview "boneco de prova".
   ───────────────────────────────────────────────────────────────────────── */

const ARMOR_PIECES = [
  { id: 'helmet',     label: 'Capacete', sprite: 'capacete', def: 3, dur: 11 },
  { id: 'chestplate', label: 'Peitoral', sprite: 'peitoral', def: 8, dur: 16 },
  { id: 'leggings',   label: 'Calças',   sprite: 'calcas',   def: 6, dur: 15 },
  { id: 'boots',      label: 'Botas',    sprite: 'botas',    def: 3, dur: 13 },
];

function armorDefense(rec) {
  return ARMOR_PIECES.reduce((s, p) => s + ((rec.pieces || []).includes(p.id) ? p.def : 0), 0);
}

// ═══ EDITOR ════════════════════════════════════════════════════════════════
function ArmorEditor({ rec, modId, set, onEditTexture }) {
  const pieces = rec.pieces || [];
  function togglePiece(id) {
    const next = pieces.includes(id) ? pieces.filter(p => p !== id) : [...pieces, id];
    const lead = ARMOR_PIECES.find(p => next.includes(p.id));
    set({ pieces: next, sprite: lead ? lead.sprite : rec.sprite });
  }

  return (
    <div>
      <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start' }}>
        <div>
          <label className="mx-label">Textura</label>
          <div className="mx-tex-pick filled" style={{ cursor: 'default' }}>
            <window.PixelSprite sprite={rec.sprite} size={58} />
            <button type="button" className="mx-tex-edit" title="Pintar textura"
              onClick={() => onEditTexture && onEditTexture(rec.sprite)}>
              <window.Icon name="pen" style="line" size={13} />
            </button>
          </div>
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ marginBottom: 18 }}>
            <window.TextField label="Nome do conjunto" value={rec.name}
              onChange={v => set({ name: v, id: rec._idEdited ? rec.id : window.toId(v) })}
              placeholder="Armadura de Rubi" hint="Cada peça vira “Capacete de Rubi”, etc." />
          </div>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: window.toId(v), _idEdited: true })} hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      {/* peças geradas */}
      <div style={{ fontSize: 12, fontWeight: 800, letterSpacing: '.8px', textTransform: 'uppercase',
        color: 'var(--text-dim)', margin: '24px 0 12px', paddingBottom: 8, borderBottom: '1px solid var(--border-sub)',
        display: 'flex', justifyContent: 'space-between' }}>
        <span>Peças do conjunto</span>
        <span style={{ fontWeight: 500, textTransform: 'none', letterSpacing: 0 }}>{pieces.length}/4 geradas</span>
      </div>
      <div className="mx-pieces">
        {ARMOR_PIECES.map(p => {
          const on = pieces.includes(p.id);
          return (
            <button type="button" key={p.id} className={`mx-piece${on ? ' on' : ''}`} onClick={() => togglePiece(p.id)}>
              <span className="mx-piece-slot"><window.PixelSprite sprite={p.sprite} size={36} /></span>
              <b>{p.label}</b>
              <span className="mx-piece-def">+{p.def} def</span>
            </button>
          );
        })}
      </div>

      {/* atributos do material */}
      <div style={{ fontSize: 12, fontWeight: 800, letterSpacing: '.8px', textTransform: 'uppercase',
        color: 'var(--text-dim)', margin: '24px 0 12px', paddingBottom: 8, borderBottom: '1px solid var(--border-sub)' }}>
        Material
      </div>
      <div className="mx-row">
        <window.NumberField label="Multiplicador de durabilidade" value={rec.durabilityMult} min={1}
          onChange={v => set({ durabilityMult: v })} hint="Ferro = 15 · Diamante = 33." />
        <window.NumberField label="Encantabilidade" value={rec.enchantability} min={0}
          onChange={v => set({ enchantability: v })} hint="Ouro = 25 · Diamante = 10." />
      </div>
      <div className="mx-row" style={{ marginTop: 16 }}>
        <window.NumberField label="Robustez (toughness)" value={rec.toughness} min={0} step={0.5}
          onChange={v => set({ toughness: v })} hint="Diamante = 2 · Netherita = 3." />
        <window.SelectField label="Raridade" value={rec.rarity} options={window.MX_RARITIES}
          onChange={v => set({ rarity: v })} />
      </div>
      <div style={{ height: 16 }} />
    </div>
  );
}

// ═══ PREVIEW: boneco de prova ═══════════════════════════════════════════════
function ArmorPreview({ rec, modId, popKey }) {
  const pieces = rec.pieces || [];
  const def = armorDefense(rec);
  const rar = (window.MX_RARITIES || []).find(r => r.id === rec.rarity) || {};

  function dollSlot(pid) {
    const p = ARMOR_PIECES.find(x => x.id === pid);
    const on = pieces.includes(pid);
    return (
      <div key={pid} className={`mx-doll-slot${on ? ' on' : ''}`} title={on ? p.label : `${p.label} — não gerada`}>
        {on ? <window.PixelSprite sprite={p.sprite} size={40} /> : <span className="mx-doll-ghost"><window.PixelSprite sprite={p.sprite} size={40} /></span>}
      </div>
    );
  }

  return (
    <aside className="mx-preview">
      <div className="mx-pv-head">
        <span className="mx-pv-title"><span className="mx-live-dot" /> Preview ao vivo</span>
        <span style={{ fontSize: 11, color: 'var(--text-dim)', fontFamily: 'var(--font-mono, monospace)' }}>armadura</span>
      </div>
      <div className="mx-pv-body">
        <div className="mx-stage">
          <div key={popKey} className="mx-pop mx-doll">
            <div className="mx-doll-col">{['helmet', 'chestplate', 'leggings', 'boots'].map(dollSlot)}</div>
            <div className="mx-doll-fig" aria-hidden="true">
              <span className={`fg-head${pieces.includes('helmet') ? ' eq' : ''}`} />
              <span className={`fg-torso${pieces.includes('chestplate') ? ' eq' : ''}`} />
              <span className={`fg-legs${pieces.includes('leggings') ? ' eq' : ''}`} />
              <span className={`fg-feet${pieces.includes('boots') ? ' eq' : ''}`} />
            </div>
          </div>
        </div>

        {/* pontos de armadura como no HUD do jogo */}
        <div className="mx-armor-hud" title={`${def} pontos de armadura`}>
          {Array.from({ length: 10 }, (_, i) => {
            const v = def - i * 2;
            return <span key={i} className={`mx-ap${v >= 2 ? ' full' : v === 1 ? ' half' : ''}`} />;
          })}
          <b>{def}</b>
        </div>

        <div className="mx-pv-meta">
          <div className="mx-meta-card">
            <div className="mx-meta-row"><span className="mx-meta-k">Nome</span>
              <span className="mx-meta-v" style={{ color: rar.css || 'var(--text)' }}>{rec.name || '—'}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Peças</span>
              <span className="mx-meta-v">{pieces.length ? pieces.map(p => (ARMOR_PIECES.find(x => x.id === p) || {}).label).join(' · ') : 'nenhuma'}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Durabilidade</span>
              <span className="mx-meta-v">{ARMOR_PIECES.filter(p => pieces.includes(p.id)).map(p => p.dur * (rec.durabilityMult || 1)).join(' · ') || '—'}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Robustez</span>
              <span className="mx-meta-v">{rec.toughness} · ench. {rec.enchantability}</span></div>
          </div>
        </div>
      </div>
    </aside>
  );
}

Object.assign(window, { ArmorEditor, ArmorPreview, ARMOR_PIECES });
