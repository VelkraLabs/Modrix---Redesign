/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Tela de Conquistas dedicada (overlay do launcher).
   Hero com progresso + XP ganho, agrupada por categoria, cards de troféu
   estilo jogo: desbloqueadas brilham, travadas em cinza com a dica.
   ───────────────────────────────────────────────────────────────────────── */

function AchievementsScreen({ onClose }) {
  const all = window.MX_ACHIEVEMENTS || [];
  const cats = window.MX_ACH_CATS || [];
  const unlocked = window.mxAchUnlocked ? window.mxAchUnlocked() : new Set(all.filter(a => a.got).map(a => a.id));
  const gotCount = all.filter(a => unlocked.has(a.id)).length;
  const pct = Math.round((gotCount / all.length) * 100);
  const xpEarned = all.filter(a => unlocked.has(a.id)).reduce((s, a) => s + (a.xp || 40), 0);
  const xpTotal = all.reduce((s, a) => s + (a.xp || 40), 0);

  // a "próxima" conquista (primeira travada) ganha um realce
  const nextId = (all.find(a => !unlocked.has(a.id)) || {}).id;

  function card(a) {
    const got = unlocked.has(a.id);
    return (
      <div key={a.id} className={`ax-card${got ? ' got' : ' locked'}${a.id === nextId ? ' next' : ''}`}>
        <div className="ax-emblem">
          <window.PixelSprite sprite={a.emblem} size={40} />
          {got
            ? <span className="ax-check"><window.Icon name="check" style="line" size={12} /></span>
            : <span className="ax-lock">🔒</span>}
        </div>
        <div className="ax-body">
          <div className="ax-name">{a.name}</div>
          <div className="ax-desc">{a.desc}</div>
        </div>
        <div className={`ax-xp${got ? ' on' : ''}`}>{got ? `+${a.xp || 40}` : `${a.xp || 40}`} XP</div>
        {a.id === nextId && <span className="ax-next-tag">próxima</span>}
      </div>
    );
  }

  return (
    <div className="ax-veil" onClick={e => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="ax-screen" data-screen-label="Conquistas">
        <button className="ax-close" onClick={onClose} title="Voltar">✕</button>

        {/* hero */}
        <div className="ax-hero">
          <div className="ax-trophy"><span>🏆</span></div>
          <div className="ax-hero-body">
            <h1>Conquistas</h1>
            <p>{gotCount} de {all.length} desbloqueadas · <b>{xpEarned}</b> de {xpTotal} XP conquistado</p>
            <div className="ax-progress"><i style={{ width: pct + '%' }} /><span className="ax-progress-pct">{pct}%</span></div>
          </div>
        </div>

        {/* corpo rolável */}
        <div className="ax-scroll">
          {cats.map(cat => {
            const list = all.filter(a => a.cat === cat.id);
            const done = list.filter(a => unlocked.has(a.id)).length;
            return (
              <section key={cat.id} className="ax-cat">
                <div className="ax-cat-head">
                  <span className="ax-cat-name">{cat.label}</span>
                  <span className="ax-cat-hint">{cat.hint}</span>
                  <span className="ax-cat-count">{done}/{list.length}</span>
                </div>
                <div className="ax-grid">{list.map(card)}</div>
              </section>
            );
          })}
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { AchievementsScreen });
