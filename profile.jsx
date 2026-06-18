/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Perfil do Ferreiro (overlay do launcher).
   Consolida o que estava espalhado: nível/XP, rank, ofensiva e conquistas,
   com a "Trilha do ferreiro" — a progressão de ranks como uma jornada — de
   destaque. Tudo derivado de mxComputeXp() + storage, sem dados inventados.
   ───────────────────────────────────────────────────────────────────────── */

function ProfileScreen({ onClose, onOpenAch }) {
  const prog = window.mxComputeXp();
  const profile = window.MX_PROFILE;
  const projects = window.mxLoadProjects ? window.mxLoadProjects() : (window.MX_PROJECTS || []);
  const streak = window.mxStreakTick ? window.mxStreakTick() : profile.streak;
  const ranks = window.MX_RANKS || [];
  const ach = window.MX_ACHIEVEMENTS || [];
  const unlocked = window.mxAchUnlocked ? window.mxAchUnlocked() : new Set(ach.filter(a => a.got).map(a => a.id));

  const content = prog.content, got = prog.got;
  const xpContent = content * 8, xpAch = got * 40, xpBonus = Math.max(0, prog.xp - xpContent - xpAch);
  const totalRecipes = projects.reduce((s, p) => s + (p.recipes || 0), 0);

  let curIdx = 0; ranks.forEach((r, i) => { if (prog.xp >= r.min) curIdx = i; });
  const span = Math.max(1, ranks.length - 1);
  const fillPct = Math.min(100, ((curIdx + (prog.next ? prog.pct / 100 : 1)) / span) * 100);

  React.useEffect(() => {
    const k = e => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  }, []);

  const bars = [
    { label: 'Conteúdo forjado', sub: `${content} registros × 8 XP`, xp: xpContent },
    { label: 'Conquistas', sub: `${got} desbloqueadas × 40 XP`, xp: xpAch },
    { label: 'Ações na oficina', sub: 'salvar, testar, pintar texturas…', xp: xpBonus },
  ];
  const maxBar = Math.max(1, ...bars.map(b => b.xp));

  return (
    <div className="ax-veil" onClick={e => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="pf-screen" data-screen-label="Perfil do ferreiro">
        <button className="ax-close" onClick={onClose} title="Voltar">✕</button>
        <div className="pf-scroll">

          {/* ── hero ── */}
          <div className="pf-hero">
            <div className="hx-ring" style={{ '--pct': prog.pct }}>
              <div className="hx-ring-inner"><window.PixelSprite sprite={profile.emblem} size={64} /></div>
              <span className="hx-lvl-badge">Nível {prog.level}</span>
            </div>
            <div className="pf-hero-body">
              <div className="pf-hero-rank">⟡ {prog.rank.title}</div>
              <h1>{profile.name}</h1>
              <div className="pf-hero-handle">{profile.handle}</div>
              <div className="pf-hero-chips">
                <span className="pf-chip"><b>{prog.xp.toLocaleString('pt-BR')}</b> XP total</span>
                <span className="pf-chip streak"><span className="mx-flame"><i /></span> {streak} {streak === 1 ? 'dia' : 'dias'} forjando</span>
                <span className="pf-chip"><b>{got}</b>/{ach.length} conquistas</span>
              </div>
            </div>
          </div>

          {/* ── trilha de ranks ── */}
          <section className="pf-sect">
            <div className="pf-sect-head">
              <h2>Trilha do ferreiro</h2>
              <span>{prog.next ? `faltam ${(prog.next.min - prog.xp).toLocaleString('pt-BR')} XP para ${prog.next.title}` : 'rank máximo alcançado — você é uma lenda'}</span>
            </div>
            <div className="pf-trail">
              <div className="pf-track"><i style={{ width: fillPct + '%' }} /></div>
              <div className="pf-nodes">
                {ranks.map((r, i) => {
                  const achd = prog.xp >= r.min, cur = i === curIdx;
                  return (
                    <div key={i} className={`pf-rank${achd ? ' on' : ''}${cur ? ' cur' : ''}`}>
                      <span className="pf-rank-node">
                        {cur ? <window.PixelSprite sprite={profile.emblem} size={24} /> : (i + 1)}
                      </span>
                      <span className="pf-rank-title">{r.title}</span>
                      <span className="pf-rank-xp">{r.min === 0 ? 'início' : `${r.min.toLocaleString('pt-BR')} XP`}</span>
                      {cur && <span className="pf-rank-you">você</span>}
                    </div>
                  );
                })}
              </div>
            </div>
          </section>

          {/* ── números + XP ── */}
          <div className="pf-cols">
            <section className="pf-panel">
              <h3>Forja em números</h3>
              <div className="hx-stats">
                <div className="hx-stat"><span className="k"><window.Icon name="folder" style="line" size={14} /> Mods na oficina</span><span className="v">{projects.length}</span></div>
                <div className="hx-stat"><span className="k"><window.Icon name="cube" style="line" size={14} /> Conteúdos forjados</span><span className="v">{content}</span></div>
                <div className="hx-stat"><span className="k"><window.Icon name="grid" style="line" size={14} /> Receitas criadas</span><span className="v">{totalRecipes}</span></div>
                <div className="hx-stat"><span className="k"><window.Icon name="spark" style="line" size={14} /> Conquistas</span><span className="v">{got}/{ach.length}</span></div>
                <div className="hx-stat"><span className="k"><span className="mx-flame" style={{ width: 13, height: 13 }}><i /></span> Ofensiva</span><span className="v">{streak}d</span></div>
              </div>
            </section>
            <section className="pf-panel">
              <h3>De onde vem seu XP</h3>
              <div className="pf-bars">
                {bars.map((b, i) => (
                  <div className="pf-bar" key={i}>
                    <div className="pf-bar-top"><span>{b.label}</span><b>{b.xp.toLocaleString('pt-BR')} XP</b></div>
                    <div className="pf-bar-track"><i style={{ width: (b.xp / maxBar * 100) + '%' }} /></div>
                    <div className="pf-bar-sub">{b.sub}</div>
                  </div>
                ))}
              </div>
            </section>
          </div>

          {/* ── mural de conquistas ── */}
          <section className="pf-sect">
            <div className="pf-sect-head">
              <h2>Mural de conquistas</h2>
              <button className="pf-link" onClick={onOpenAch}>ver todas <window.Icon name="chevron" style="line" size={12} fallback="grid" /></button>
            </div>
            <div className="pf-badges">
              {ach.map(a => {
                const g = unlocked.has(a.id);
                return (
                  <div key={a.id} className={`hx-badge ${g ? 'got' : 'locked'}`} onClick={onOpenAch} style={{ cursor: 'pointer' }}>
                    <window.PixelSprite sprite={a.emblem} size={26} />
                    <span className="tip">
                      <b>{a.name}</b>
                      <span>{a.desc}</span>
                      {!g && <span className="lk">bloqueada</span>}
                    </span>
                  </div>
                );
              })}
            </div>
          </section>

          {/* ── coleção ── */}
          <section className="pf-sect">
            <div className="pf-sect-head">
              <h2>Sua coleção</h2>
              <span>{projects.length} {projects.length === 1 ? 'mod' : 'mods'}</span>
            </div>
            <div className="pf-mods">
              {projects.slice(0, 8).map(p => (
                <div className="pf-mod" key={p.id} style={{ '--pa': p.accent }}>
                  <span className="pf-mod-emblem"><window.PixelSprite sprite={p.emblem} size={26} /></span>
                  <span style={{ minWidth: 0 }}>
                    <div className="pf-mod-name">{p.name}</div>
                    <div className="pf-mod-meta">v{p.version} · {(p.items || 0) + (p.blocks || 0) + (p.recipes || 0)} reg.</div>
                  </span>
                </div>
              ))}
            </div>
          </section>

        </div>
      </div>
    </div>
  );
}

Object.assign(window, { ProfileScreen });
