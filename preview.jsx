/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Preview ao vivo (painel direito, o recurso-herói)
   Renderiza o conteúdo em edição como apareceria no jogo, ao vivo.
   Estilo INSPIRADO em jogos sandbox — visual original, não a UI do Minecraft.
   ───────────────────────────────────────────────────────────────────────── */
const { useState: usePvState } = React;

function rarityCss(id) {
  return ({ COMMON: 'var(--rar-common)', UNCOMMON: 'var(--rar-uncommon)',
    RARE: 'var(--rar-rare)', EPIC: 'var(--rar-epic)' })[id] || 'var(--rar-common)';
}

// Bloco isométrico montado de 3 faces (mesma textura, brilhos diferentes via CSS)
function IsoBlock({ sprite }) {
  return (
    <div className="mx-iso">
      <CanvasFace sprite={sprite} cls="face top" />
      <CanvasFace sprite={sprite} cls="face left" />
      <CanvasFace sprite={sprite} cls="face right" />
    </div>
  );
}
function CanvasFace({ sprite, cls }) {
  return (
    <div className={cls}>
      <window.PixelSprite sprite={sprite} size={70} />
    </div>
  );
}

// Tooltip estilo jogo — espelha os campos do form
function GameTooltip({ rec, modId }) {
  if (rec.kind === 'block') {
    return (
      <div className="mx-tooltip">
        <div className="mx-tt-name" style={{ color: 'var(--rar-common)' }}>{rec.name || 'Sem nome'}</div>
        <div className="mx-tt-line gray">Bloco</div>
        {rec.requiresTool && <div className="mx-tt-line">Requer {rec.tool === 'pickaxe' ? 'picareta' : rec.tool} ({rec.toolTier})</div>}
        <div className="mx-tt-line">Dureza {rec.hardness} · Resistência {rec.resistance}</div>
        {rec.lightLevel > 0 && <div className="mx-tt-line" style={{ color: 'var(--rar-uncommon)' }}>Emite luz {rec.lightLevel}</div>}
        <div className="mx-tt-id">{modId}:{rec.id}</div>
      </div>
    );
  }
  const enchantGlow = rec.enchantable && rec.rarity === 'EPIC';
  return (
    <div className="mx-tooltip" style={enchantGlow ? { boxShadow: '0 0 22px color-mix(in srgb, var(--rar-epic) 35%, transparent), 0 8px 24px rgba(0,0,0,.5)' } : null}>
      <div className="mx-tt-name" style={{ color: rarityCss(rec.rarity) }}>{rec.name || 'Sem nome'}</div>
      {rec.consumable === 'food'
        ? <div className="mx-tt-line green">Restaura {rec.nutrition} de fome · {rec.saturation} saturação</div>
        : <div className="mx-tt-line gray">Quando na mão principal:</div>}
      {rec.consumable !== 'food' && rec.attackDamage > 0 && (
        <>
          <div className="mx-tt-line green"> {rec.attackDamage} de Dano de Ataque</div>
          <div className="mx-tt-line green"> {rec.attackSpeed} de Velocidade de Ataque</div>
        </>
      )}
      {rec.fireResistant && <div className="mx-tt-line blue">Imune ao fogo e à lava</div>}
      {rec.durability > 0 && (
        <>
          <div className="mx-tt-line gray" style={{ marginTop: 6 }}>Durabilidade: {rec.durability} / {rec.durability}</div>
          <div className="mx-durbar"><i style={{ width: '100%', background: 'var(--green)' }} /></div>
        </>
      )}
      {enchantGlow && <div className="mx-tt-line" style={{ color: 'var(--rar-epic)', marginTop: 6 }}>✦ Encantável</div>}
      <div className="mx-tt-id">{modId}:{rec.id}</div>
    </div>
  );
}

function LivePreview({ rec, modId, defaultTab, popKey, fxKey, gameFeel }) {
  const isBlock = rec.kind === 'block';
  const tabs = isBlock
    ? [{ id: 'world', label: 'No mundo' }, { id: 'slot', label: 'Inventário' }, { id: 'tip', label: 'Tooltip' }]
    : [{ id: 'slot', label: 'Inventário' }, { id: 'hand', label: 'Na mão' }, { id: 'tip', label: 'Tooltip' }];
  const [tab, setTab] = usePvState(defaultTab && tabs.some(t => t.id === defaultTab) ? defaultTab : tabs[0].id);
  // se trocar de conteúdo e a aba não existir, volta pra primeira
  React.useEffect(() => { if (!tabs.some(t => t.id === tab)) setTab(tabs[0].id); }, [isBlock]);

  const sprite = rec.sprite;
  const glint = !isBlock && rec.enchantable && rec.rarity === 'EPIC';

  // partículas pixel ao salvar (game feel)
  const [parts, setParts] = usePvState([]);
  const firstFx = React.useRef(true);
  React.useEffect(() => {
    if (firstFx.current) { firstFx.current = false; return; }
    if (!gameFeel || !fxKey) return;
    const colors = ['#ff7d97', '#e1395b', '#f5c46a', 'var(--accent)', '#ffffff'];
    const ps = Array.from({ length: 16 }, (_, i) => ({
      id: `${fxKey}-${i}`,
      bg: colors[i % colors.length],
      dx: `${Math.round((Math.random() * 2 - 1) * 130)}px`,
      dy: `${Math.round((Math.random() * 2 - 1) * 110 - 25)}px`,
      delay: `${Math.round(Math.random() * 90)}ms`,
      size: 4 + Math.round(Math.random() * 5),
    }));
    setParts(ps);
    const t = setTimeout(() => setParts([]), 900);
    return () => clearTimeout(t);
  }, [fxKey, gameFeel]);

  return (
    <aside className="mx-preview">
      <div className="mx-pv-head">
        <span className="mx-pv-title"><span className="mx-live-dot" /> Preview ao vivo</span>
        <span style={{ fontSize: 11, color: 'var(--text-dim)', fontFamily: 'var(--font-mono, monospace)' }}>
          {isBlock ? 'bloco' : 'item'}
        </span>
      </div>

      <div className="mx-pv-tabs">
        <window.Segmented options={tabs} value={tab} onChange={setTab} />
      </div>

      <div className="mx-pv-body">
        {/* ── Palco ───────────────────────────────────────────────── */}
        <div className="mx-stage">
          {parts.map(p => (
            <span key={p.id} className="mx-particle"
              style={{ background: p.bg, width: p.size, height: p.size,
                '--dx': p.dx, '--dy': p.dy, animationDelay: p.delay }} />
          ))}
          {tab === 'slot' && (
            <div key={popKey} className="mx-pop mx-slot">
              {isBlock
                ? <window.PixelSprite sprite={sprite} size={56} />
                : <window.PixelSprite sprite={sprite} size={62} glint={glint} />}
              {(!isBlock && rec.stackSize > 1) && <span className="mx-stack-badge">{rec.stackSize}</span>}
            </div>
          )}
          {tab === 'hand' && (
            <div key={popKey} className="mx-pop" style={{ transform: 'rotate(-32deg)' }}>
              <window.PixelSprite sprite={sprite} size={150} glint={glint} />
            </div>
          )}
          {tab === 'world' && (
            <div key={popKey} className="mx-pop"><IsoBlock sprite={sprite} /></div>
          )}
          {tab === 'tip' && (
            <div key={popKey} className="mx-pop"><GameTooltip rec={rec} modId={modId} /></div>
          )}
        </div>

        {/* ── Meta abaixo do palco ────────────────────────────────── */}
        <div className="mx-pv-meta">
          {!isBlock ? (
            <div className="mx-meta-card">
              <div className="mx-meta-row"><span className="mx-meta-k">Raridade</span>
                <span className="mx-meta-v" style={{ color: rarityCss(rec.rarity) }}>
                  {(window.MX_RARITIES.find(r => r.id === rec.rarity) || {}).label}
                </span></div>
              <div className="mx-meta-row"><span className="mx-meta-k">Pilha máx.</span><span className="mx-meta-v">{rec.stackSize}</span></div>
              {rec.durability > 0 && <div className="mx-meta-row"><span className="mx-meta-k">Durabilidade</span><span className="mx-meta-v">{rec.durability}</span></div>}
              {rec.consumable === 'food' && <div className="mx-meta-row"><span className="mx-meta-k">Nutrição</span><span className="mx-meta-v">{rec.nutrition}</span></div>}
              {rec.consumable !== 'food' && rec.attackDamage > 0 && <div className="mx-meta-row"><span className="mx-meta-k">Dano</span><span className="mx-meta-v">{rec.attackDamage}</span></div>}
            </div>
          ) : (
            <div className="mx-meta-card">
              <div className="mx-meta-row"><span className="mx-meta-k">Dureza</span><span className="mx-meta-v">{rec.hardness}</span></div>
              <div className="mx-meta-row"><span className="mx-meta-k">Ferramenta</span><span className="mx-meta-v">{rec.requiresTool ? rec.tool : '—'}</span></div>
              <div className="mx-meta-row"><span className="mx-meta-k">Dropa</span><span className="mx-meta-v">{rec.drops}</span></div>
              <div className="mx-meta-row"><span className="mx-meta-k">XP</span><span className="mx-meta-v">{rec.xpMin}–{rec.xpMax}</span></div>
            </div>
          )}
          <div style={{ fontSize: 11, color: 'var(--text-dim)', textAlign: 'center', lineHeight: 1.5 }}>
            Atualiza enquanto você edita — nada é salvo até clicar em <b style={{ color: 'var(--text-muted)' }}>Salvar</b>.
          </div>
        </div>
      </div>
    </aside>
  );
}

Object.assign(window, { LivePreview });
