/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Editor de Mobs (EntityType) + preview com o sprite do mob,
   barra de vida em corações e atributos calculados.
   ───────────────────────────────────────────────────────────────────────── */

// ═══ EDITOR ════════════════════════════════════════════════════════════════
function MobEditor({ rec, modId, set, onEditTexture }) {
  const items = (window.MX_MOD && window.MX_MOD.items) || [];
  const dropOpts = [{ id: 'rubi', label: 'Rubi' }, ...items.map(i => ({ id: i.id, label: i.name })), { id: '', label: '— nada —' }];

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
            <window.TextField label="Nome do mob" value={rec.name}
              onChange={v => set({ name: v, id: rec._idEdited ? rec.id : window.toId(v) })}
              placeholder="Golem de Rubi" />
          </div>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: window.toId(v), _idEdited: true })} hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      <div className="mx-eff-section">Comportamento</div>
      <window.Segmented value={rec.category}
        options={[{ id: 'passive', label: 'Pacífico' }, { id: 'neutral', label: 'Neutro' }, { id: 'hostile', label: 'Hostil' }]}
        onChange={v => set({ category: v })} />

      <div className="mx-eff-section">Atributos</div>
      <div className="mx-row">
        <window.NumberField label="Vida (HP)" value={rec.health} min={1} max={1024} step={2}
          onChange={v => set({ health: v })} hint="2 HP = 1 coração. Zumbi = 20." />
        <window.NumberField label="Dano de ataque" value={rec.damage} min={0} step={0.5}
          onChange={v => set({ damage: v })} hint="Em pontos. Zumbi = 3." />
      </div>
      <div className="mx-row" style={{ marginTop: 16 }}>
        <window.NumberField label="Velocidade" value={rec.speed} min={0} max={1} step={0.01}
          onChange={v => set({ speed: v })} hint="0.23 = padrão. 0.0 = imóvel." />
        <window.NumberField label="Armadura" value={rec.armor} min={0} max={30}
          onChange={v => set({ armor: v })} hint="Reduz dano recebido." />
      </div>
      <div className="mx-row" style={{ marginTop: 16 }}>
        <window.NumberField label="Resist. a empurrão" value={rec.knockbackRes} min={0} max={1} step={0.1}
          onChange={v => set({ knockbackRes: v })} hint="0–1. Golem = 1." />
        <window.NumberField label="XP ao morrer" value={rec.xpDrop} min={0}
          onChange={v => set({ xpDrop: v })} hint="Esferas de experiência." />
      </div>

      <div className="mx-eff-section">Drops</div>
      <div className="mx-row">
        <window.SelectField label="Item dropado" value={rec.dropItem} options={dropOpts}
          onChange={v => set({ dropItem: v })} hint="O que cai ao morrer." />
        <div className="mx-row" style={{ gap: 12 }}>
          <window.NumberField label="Mín." value={rec.dropMin} min={0}
            onChange={v => set({ dropMin: Math.min(v, rec.dropMax) })} />
          <window.NumberField label="Máx." value={rec.dropMax} min={0}
            onChange={v => set({ dropMax: Math.max(v, rec.dropMin) })} />
        </div>
      </div>

      <div className="mx-eff-section">Geração</div>
      <div className="mx-box">
        <window.CheckField label="Surge naturalmente" hint="Aparece no mundo sem invocação."
          checked={rec.spawns} onChange={v => set({ spawns: v })} />
        {rec.spawns && (
          <div style={{ marginTop: 14 }}>
            <window.NumberField label="Nível de luz máximo" value={rec.spawnLight} min={0} max={15}
              onChange={v => set({ spawnLight: v })} hint="0 = só no escuro. 15 = qualquer luz." />
          </div>
        )}
      </div>
      <div style={{ height: 16 }} />
    </div>
  );
}

// ═══ PREVIEW ═══════════════════════════════════════════════════════════════
function MobPreview({ rec, modId, popKey }) {
  const hearts = Math.ceil(rec.health / 2);
  const cap = Math.min(hearts, 50);
  const catLabel = rec.category === 'hostile' ? 'Hostil' : rec.category === 'passive' ? 'Pacífico' : 'Neutro';
  const catColor = rec.category === 'hostile' ? 'var(--red)' : rec.category === 'passive' ? 'var(--green)' : 'var(--yellow)';
  const dropName = rec.dropItem === 'rubi' ? 'Rubi' : ((window.MX_MOD.items || []).find(i => i.id === rec.dropItem) || {}).name || '—';

  return (
    <aside className="mx-preview">
      <div className="mx-pv-head">
        <span className="mx-pv-title"><span className="mx-live-dot" /> Preview ao vivo</span>
        <span style={{ fontSize: 11, color: 'var(--text-dim)', fontFamily: 'var(--font-mono, monospace)' }}>mob</span>
      </div>
      <div className="mx-pv-body">
        <div className="mx-stage">
          <div key={popKey} className="mx-pop mx-mob-stage">
            <span className="mx-mob-shadow" />
            <span className="mx-mob-fig"><window.PixelSprite sprite={rec.sprite} size={92} /></span>
          </div>
        </div>

        {/* nome + corações */}
        <div className="mx-mob-namebar">
          <span className="mx-mob-name" style={{ color: catColor }}>{rec.name || 'Mob'}</span>
        </div>
        <div className="mx-mob-hearts" title={`${rec.health} HP`}>
          {Array.from({ length: cap }, (_, i) => <span key={i} className="mx-heart" />)}
          {hearts > cap && <span className="mx-mob-hpmore">+{hearts - cap}</span>}
          <b>{rec.health} HP</b>
        </div>

        <div className="mx-pv-meta">
          <div className="mx-meta-card">
            <div className="mx-meta-row"><span className="mx-meta-k">Comportamento</span>
              <span className="mx-meta-v" style={{ color: catColor }}>{catLabel}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Ataque</span>
              <span className="mx-meta-v">{rec.damage} dano · {rec.armor} armadura</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Velocidade</span>
              <span className="mx-meta-v">{rec.speed} · empurrão {rec.knockbackRes}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Drop</span>
              <span className="mx-meta-v">{dropName}{rec.dropItem ? ` ×${rec.dropMin}–${rec.dropMax}` : ''} · {rec.xpDrop} XP</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Surge</span>
              <span className="mx-meta-v">{rec.spawns ? `sim · luz ≤ ${rec.spawnLight}` : 'só invocado'}</span></div>
          </div>
        </div>
      </div>
    </aside>
  );
}

Object.assign(window, { MobEditor, MobPreview });
