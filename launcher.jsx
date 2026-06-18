/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Tela inicial (launcher) estilo menu de jogo.
   Carteira do ferreiro (nível/XP/ofensiva), conquistas, "Forjar novo mod",
   e projetos como saves com menu de contexto (renomear/duplicar/excluir).
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useHxState, useMemo: useHxMemo, useEffect: useHxEffect, useRef: useHxRef } = React;

const HX_THEMES = [
  { id: 'pro',      dot: '#2dd4bf', label: 'Refino Pro' },
  { id: 'workshop', dot: '#e89b46', label: 'Workshop' },
  { id: 'forge',    dot: '#4cc2ff', label: 'Forge' },
  { id: 'beginner', dot: '#12a594', label: 'Iniciante' },
];
const HX_ACCENTS = ['#2dd4bf', '#5fa9ff', '#e89b46', '#5ce0a3', '#a78bfa'];

function HxArrow({ size = 20 }) {
  return (
    <svg width={size + 10} height={size} viewBox="0 0 44 30" style={{ display: 'block' }} aria-hidden="true">
      <path d="M2 15 H34 M26 6 L36 15 L26 24" fill="none" stroke="currentColor" strokeWidth="3"
        strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}
function hxGreeting() {
  const h = new Date().getHours();
  return h < 12 ? 'Bom dia' : h < 18 ? 'Boa tarde' : 'Boa noite';
}
function hxClone(o) { return JSON.parse(JSON.stringify(o)); }
function hxSetPending(p) {
  try { localStorage.setItem('modrix_open_project_v1', JSON.stringify(p)); } catch (e) {}
}

/* ── Overlay de abertura (save → workspace) ────────────────────────────── */
function LaunchOverlay({ info }) {
  useHxEffect(() => {
    const t = setTimeout(() => { window.location.href = 'Modrix Workspace.html'; }, 1250);
    return () => clearTimeout(t);
  }, []);
  return (
    <div className="hx-launch" style={{ '--pa': info.accent }}>
      <div className="hx-launch-emblem"><window.PixelSprite sprite={info.emblem} size={66} /></div>
      <div style={{ textAlign: 'center' }}>
        <div className="hx-launch-txt">{info.mode === 'forge' ? `Forjando “${info.name}”…` : `Abrindo ${info.name}`}</div>
        <div className="hx-launch-sub">{info.mode === 'forge' ? 'gerando projeto NeoForge + Gradle' : 'carregando oficina'}</div>
      </div>
      <div className="bar"><i /></div>
    </div>
  );
}

/* ── Modal "Forjar novo mod" ───────────────────────────────────────────── */
function CreateModal({ onClose, onForge }) {
  const [name, setName] = useHxState('');
  const [kit, setKit] = useHxState('items');
  const [accent, setAccent] = useHxState(HX_ACCENTS[0]);
  const modId = window.toId ? window.toId(name) : name.toLowerCase().replace(/[^a-z0-9]+/g, '_');
  const kitObj = window.MX_TEMPLATES.find(k => k.id === kit);

  return (
    <div className="hx-veil" onClick={e => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="hx-modal" data-screen-label="Modal Forjar novo mod">
        <button className="hx-close" onClick={onClose} title="Fechar">✕</button>
        <h2>Forjar um novo mod</h2>
        <div className="sub">Dê um nome, escolha um kit inicial e bata o martelo.</div>

        <label className="mx-label">Nome do mod</label>
        <input className="mx-field-input" autoFocus value={name} placeholder="Reinos de Cristal"
          onChange={e => setName(e.target.value)} />
        <div className="mx-hint" style={{ fontFamily: 'var(--font-mono, monospace)', fontStyle: 'normal' }}>
          id: {modId || '…'} · NeoForge 26.1.4 · Java 21
        </div>

        <div style={{ height: 18 }} />
        <label className="mx-label">Kit inicial</label>
        <div className="hx-kits">
          {window.MX_TEMPLATES.map(k => (
            <button type="button" key={k.id} className={`hx-kit${kit === k.id ? ' on' : ''}`} onClick={() => setKit(k.id)}>
              <span className="hx-kit-ico"><window.PixelSprite sprite={k.emblem} size={26} /></span>
              <span style={{ minWidth: 0 }}>
                <b>{k.name}</b>
                <span>{k.desc}</span>
              </span>
            </button>
          ))}
        </div>

        <div style={{ height: 16 }} />
        <label className="mx-label">Cor do mod</label>
        <div className="hx-swatches">
          {HX_ACCENTS.map(c => (
            <button type="button" key={c} className={`hx-swatch${accent === c ? ' on' : ''}`}
              style={{ '--sw': c }} onClick={() => setAccent(c)} title={c} />
          ))}
        </div>

        <div className="hx-modal-foot">
          <button className="hx-forge-btn" disabled={!name.trim()}
            onClick={() => onForge({ name: name.trim(), modId, kit, emblem: kitObj.emblem, accent, mode: 'forge' })}>
            <window.Icon name="spark" style="line" size={15} /> Forjar mod
          </button>
          <button className="hx-cancel-btn" onClick={onClose}>Cancelar</button>
          {!name.trim() && <span style={{ fontSize: 11, color: 'var(--text-dim)', fontStyle: 'italic' }}>todo mod precisa de um nome</span>}
        </div>
      </div>
    </div>
  );
}

/* ── Cartão de projeto (save) ──────────────────────────────────────────── */
function ProjectCard({ p, top, renaming, onOpen, onMenu, onRename }) {
  const inputRef = useHxRef(null);
  useHxEffect(() => { if (renaming && inputRef.current) { inputRef.current.focus(); inputRef.current.select(); } }, [renaming]);

  return (
    <div className="hx-proj" style={{ '--pa': p.accent }}
      onClick={() => { if (!renaming) onOpen(p); }}
      onContextMenu={e => { e.preventDefault(); onMenu(p, e.clientX, e.clientY); }}
      data-screen-label={`Projeto ${p.name}`}>
      <div className="hx-proj-top">
        <div className="hx-proj-emblem"><window.PixelSprite sprite={p.emblem} size={40} /></div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          {top
            ? <span className="hx-continue"><span className="mx-live-dot" /> Continuar</span>
            : <span className="hx-nf">NF {p.neoforge}</span>}
          <button className="hx-kebab" title="Opções"
            onClick={e => { e.stopPropagation(); const r = e.currentTarget.getBoundingClientRect(); onMenu(p, r.left, r.bottom + 6); }}>⋯</button>
        </div>
      </div>
      {renaming ? (
        <input ref={inputRef} className="hx-rename" defaultValue={p.name}
          onClick={e => e.stopPropagation()}
          onKeyDown={e => {
            if (e.key === 'Enter') onRename(p.id, e.currentTarget.value);
            if (e.key === 'Escape') onRename(p.id, null);
          }}
          onBlur={e => onRename(p.id, e.currentTarget.value)} />
      ) : (
        <div className="hx-proj-name">{p.name}</div>
      )}
      <div className="hx-proj-id">{p.id} · v{p.version}</div>
      <div className="hx-counts">
        <span className="hx-count"><window.Icon name="cube" style="line" size={13} /> <b>{p.items}</b></span>
        <span className="hx-count"><window.Icon name="brick" style="line" size={13} /> <b>{p.blocks}</b></span>
        <span className="hx-count"><window.Icon name="grid" style="line" size={13} /> <b>{p.recipes}</b></span>
      </div>
      <div className="hx-proj-foot">
        <span className="hx-edited">{p.edited}</span>
        <button className="hx-open"><window.Icon name="run" style="line" size={11} /> Abrir</button>
      </div>
    </div>
  );
}

/* ── Launcher ──────────────────────────────────────────────────────────── */
function Launcher() {
  const [theme, setTheme] = useHxState(() => {
    try { return localStorage.getItem('modrix_home_theme') || 'pro'; } catch (e) { return 'pro'; }
  });
  useHxEffect(() => { try { localStorage.setItem('modrix_home_theme', theme); } catch (e) {} }, [theme]);

  const [projects, setProjects] = useHxState(() => (window.mxLoadProjects ? window.mxLoadProjects() : hxClone(window.MX_PROJECTS)));
  // persiste qualquer mudança na lista (renomear, duplicar, excluir, forjar)
  useHxEffect(() => { if (window.mxSaveProjects) window.mxSaveProjects(projects); }, [projects]);
  const [q, setQ] = useHxState('');
  const [sort, setSort] = useHxState('recent');
  const [modal, setModal] = useHxState(false);
  const [launch, setLaunch] = useHxState(null);
  const [menu, setMenu] = useHxState(null);          // {p, x, y}
  const [renamingId, setRenamingId] = useHxState(null);
  const [achOpen, setAchOpen] = useHxState(false);
  const [profileOpen, setProfileOpen] = useHxState(false);
  const [toast, setToast] = useHxState(null);        // {msg, action?:{label,fn}}
  const toastTimer = useHxRef(null);

  function showToast(tt, ms = 4200) {
    setToast(tt); clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(() => setToast(null), ms);
  }

  // fecha o menu em clique fora / Esc
  useHxEffect(() => {
    if (!menu) return;
    const close = () => setMenu(null);
    const onKey = e => { if (e.key === 'Escape') setMenu(null); };
    window.addEventListener('click', close);
    window.addEventListener('keydown', onKey);
    return () => { window.removeEventListener('click', close); window.removeEventListener('keydown', onKey); };
  }, [menu]);

  const prog = window.mxComputeXp();
  const profile = window.MX_PROFILE;
  const streak = window.mxStreakTick ? window.mxStreakTick() : profile.streak;

  // ofensiva de 7 dias → conquista "Semana de Fogo"
  useHxEffect(() => {
    if (streak >= 7 && window.mxUnlockAch) {
      const a = window.mxUnlockAch('streak7');
      if (a) showToast({ msg: `Conquista desbloqueada: ${a.name}! 🔥 +40 XP` }, 5000);
    }
  }, []);

  const embers = useHxMemo(() => Array.from({ length: 22 }, (_, i) => ({
    id: i,
    left: Math.random() * 100,
    size: 3 + Math.round(Math.random() * 3),
    dur: 10 + Math.random() * 10,
    delay: Math.random() * 14,
    sway: Math.round((Math.random() * 2 - 1) * 70),
    o: 0.10 + Math.random() * 0.22,
    bg: ['var(--accent)', '#ff8a1e', '#ffffff', 'var(--accent)'][i % 4],
  })), []);

  const query = q.trim().toLowerCase();
  let shown = projects.filter(p => !query || p.name.toLowerCase().includes(query) || p.id.includes(query));
  if (sort === 'az') shown = [...shown].sort((a, b) => a.name.localeCompare(b.name));
  const topId = projects[0] ? projects[0].id : null;

  // ── ações ──
  function openProject(p) {
    hxSetPending({ type: 'existing', id: p.id, name: p.name, version: p.version, accent: p.accent });
    setLaunch({ name: p.name, emblem: p.emblem, accent: p.accent, mode: 'open' });
  }
  function forge(info) {
    const id = uniqueProjId(info.modId || 'meu_mod');
    const kit = (window.MX_KITS && window.MX_KITS[info.kit]) || { items: [], blocks: [], recipes: [] };
    const proj = {
      id, name: info.name, emblem: info.emblem, accent: info.accent,
      version: '0.1.0', neoforge: '26.1.4',
      items: kit.items.length, blocks: kit.blocks.length, recipes: kit.recipes.length,
      edited: 'agora mesmo', editedRank: -1,
    };
    setProjects(ps => [proj, ...ps]);
    hxSetPending({ type: 'new', name: info.name, modId: id, kit: info.kit, accent: info.accent });
    setModal(false); setLaunch(info);
  }
  function uniqueProjId(base) {
    const ids = new Set(projects.map(p => p.id));
    if (!ids.has(base)) return base;
    let n = 2; while (ids.has(`${base}_${n}`)) n++; return `${base}_${n}`;
  }
  function duplicate(p) {
    const copy = { ...hxClone(p), id: uniqueProjId(p.id + '_copia'), name: `${p.name} (cópia)`, edited: 'agora mesmo' };
    if (window.mxLoadMod && window.mxSaveMod) {
      const content = window.mxLoadMod(p.id);
      if (content) window.mxSaveMod(copy.id, { ...content, modId: copy.id, modName: copy.name });
    }
    setProjects(ps => [copy, ...ps]);
    showToast({ msg: `“${copy.name}” criado` }, 2600);
  }
  function remove(p) {
    const idx = projects.findIndex(x => x.id === p.id);
    setProjects(ps => ps.filter(x => x.id !== p.id));
    showToast({
      msg: `“${p.name}” excluído`,
      action: { label: 'Desfazer', fn: () => {
        setProjects(ps => { const c = [...ps]; c.splice(Math.min(idx, ps.length), 0, p); return c; });
        setToast(null);
      }},
    });
  }
  function commitRename(id, value) {
    setRenamingId(null);
    if (value === null) return;
    const name = value.trim();
    if (!name) return;
    setProjects(ps => ps.map(p => p.id === id ? { ...p, name } : p));
  }

  const totalRecipes = projects.reduce((s, p) => s + (p.recipes || 0), 0);
  const unlockedSet = window.mxAchUnlocked ? window.mxAchUnlocked() : new Set(window.MX_ACHIEVEMENTS.filter(a => a.got).map(a => a.id));
  const gotCount = unlockedSet.size;

  return (
    <div className="hx-root" data-theme={theme} data-screen-label="Início">
      {/* brasas flutuando */}
      <div className="hx-embers">
        {embers.map(e => (
          <span key={e.id} className="hx-ember" style={{
            left: `${e.left}%`, width: e.size, height: e.size, background: e.bg,
            animationDuration: `${e.dur}s`, animationDelay: `${e.delay}s`,
            '--sway': `${e.sway}px`, '--o': e.o,
          }} />
        ))}
      </div>

      {/* ── Top bar ── */}
      <header className="hx-top">
        <span className="hx-brand">
          <svg width="17" height="17" viewBox="0 0 15 15" aria-hidden="true">
            <rect x="5" y="0" width="5" height="5" fill="var(--accent)" opacity="0.5"></rect>
            <rect x="0" y="5" width="5" height="5" fill="var(--accent)"></rect>
            <rect x="10" y="5" width="5" height="5" fill="var(--accent)" opacity="0.75"></rect>
            <rect x="5" y="10" width="5" height="5" fill="var(--accent)" opacity="0.3"></rect>
          </svg>
          MODRIX <span className="v">v2.4 · estúdio de mods</span>
        </span>
        <div className="hx-top-right">
          <div className="hx-themes" title="Tema da interface">
            {HX_THEMES.map(t => (
              <button key={t.id} className={`hx-theme-dot${theme === t.id ? ' on' : ''}`}
                style={{ '--dot': t.dot }} onClick={() => setTheme(t.id)} title={t.label} />
            ))}
          </div>
        </div>
      </header>

      <div className="hx-body">
        {/* ── Rail: carteira do ferreiro ── */}
        <aside className="hx-rail">
          <div className="hx-card hx-player" data-screen-label="Carteira do ferreiro"
            onClick={() => setProfileOpen(true)} title="Abrir perfil do ferreiro">
            <div className="hx-ring" style={{ '--pct': prog.pct }}>
              <div className="hx-ring-inner"><window.PixelSprite sprite={profile.emblem} size={58} /></div>
              <span className="hx-lvl-badge">Nível {prog.level}</span>
            </div>
            <div className="hx-pname">{profile.name}</div>
            <div className="hx-phandle">{profile.handle}</div>
            <div className="hx-rank">⟡ {prog.rank.title}</div>
            <div className="hx-xpbar"><i style={{ width: `${prog.pct}%` }} /></div>
            <div className="hx-xpnums">
              <span>{prog.xp} XP</span>
              <span>{prog.next ? `${prog.next.min} → ${prog.next.title}` : 'rank máximo'}</span>
            </div>
            <div className="hx-streak" title={`Último dia: hoje · ${7 - Math.min(streak, 7) > 0 ? `faltam ${7 - streak} dias pra Semana de Fogo` : 'Semana de Fogo!'}`}>
              <span className="mx-flame"><i /></span>
              <span>{streak === 1 ? '1 dia forjando' : `${streak} dias forjando`}</span>
            </div>
            <div className="hx-view-profile">ver perfil completo <window.Icon name="chevron" style="line" size={12} fallback="grid" /></div>
          </div>

          <div className="hx-card">
            <button type="button" className="hx-card-label hx-card-label-btn" onClick={() => setAchOpen(true)} title="Ver todas as conquistas">
              Conquistas <span className="sub">{gotCount}/{window.MX_ACHIEVEMENTS.length}</span>
              <span className="hx-card-more">ver todas <window.Icon name="chevron" style="line" size={12} fallback="grid" /></span>
            </button>
            <div className="hx-shelf">
              {window.MX_ACHIEVEMENTS.map(a => {
                const got = unlockedSet.has(a.id);
                return (
                  <div key={a.id} className={`hx-badge ${got ? 'got' : 'locked'}`} onClick={() => setAchOpen(true)} style={{ cursor: 'pointer' }}>
                    <window.PixelSprite sprite={a.emblem} size={26} />
                    <span className="tip">
                      <b>{a.name}</b>
                      <span>{a.desc}</span>
                      {!got && <span className="lk">bloqueada</span>}
                    </span>
                  </div>
                );
              })}
            </div>
          </div>

          <div className="hx-card">
            <div className="hx-card-label">Oficina</div>
            <div className="hx-stats">
              <div className="hx-stat"><span className="k"><window.Icon name="folder" style="line" size={14} /> Mods</span><span className="v">{projects.length}</span></div>
              <div className="hx-stat"><span className="k"><window.Icon name="cube" style="line" size={14} /> Conteúdos forjados</span><span className="v">{prog.content}</span></div>
              <div className="hx-stat"><span className="k"><window.Icon name="grid" style="line" size={14} /> Receitas criadas</span><span className="v">{totalRecipes}</span></div>
            </div>
          </div>
        </aside>

        {/* ── Main ── */}
        <main className="hx-main">
          <div className="hx-greet">
            <h1>{hxGreeting()}, {profile.name}.</h1>
            <p>A fornalha está acesa — o que vamos forjar hoje?</p>
          </div>

          {/* Forjar novo mod */}
          <div className="hx-forge" onClick={() => setModal(true)} data-screen-label="Forjar novo mod">
            <div className="hx-forge-txt">
              <h2>Forjar novo mod</h2>
              <p>Nome, kit inicial e pronto: o Modrix monta o projeto NeoForge inteiro pra você começar criando, não configurando.</p>
              <button className="hx-forge-cta"><window.Icon name="spark" style="line" size={14} /> Começar a forjar</button>
            </div>
            <div className="hx-minicraft" aria-hidden="true">
              <div className="hx-minigrid">
                {[0, 1, 2, 3, 4, 5, 6, 7, 8].map(i => {
                  const ing = { 1: 'rubi', 4: 'rubi', 7: 'vara' }[i];
                  return (
                    <span key={i} className={`hx-minicell${ing ? ' ing' : ''}`} style={{ '--d': `${i * 0.04}s` }}>
                      {ing && <window.PixelSprite sprite={ing} size={20} />}
                    </span>
                  );
                })}
              </div>
              <span className="hx-miniarrow"><HxArrow /></span>
              <div className="hx-miniresult"><window.PixelSprite sprite="sword" size={30} /></div>
            </div>
          </div>

          {/* Seus projetos */}
          <div className="hx-sect">
            <h3>Seus projetos <b>· {shown.length}</b></h3>
            <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
              <window.Segmented value={sort}
                options={[{ id: 'recent', label: 'Recentes' }, { id: 'az', label: 'A–Z' }]}
                onChange={setSort} />
              <input className="hx-search" placeholder="Buscar projeto…" value={q} onChange={e => setQ(e.target.value)} />
            </div>
          </div>

          <div className="hx-grid">
            {shown.map(p => (
              <ProjectCard key={p.id} p={p} top={p.id === topId} renaming={renamingId === p.id}
                onOpen={openProject} onRename={commitRename}
                onMenu={(proj, x, y) => setMenu({ p: proj, x, y })} />
            ))}
          </div>
          {shown.length === 0 && (
            <div style={{ padding: '40px 0', textAlign: 'center', color: 'var(--text-dim)', fontSize: 13 }}>
              {projects.length === 0 ? 'Sua oficina está vazia — forje seu primeiro mod acima.' : `Nenhum projeto bate com “${q}”.`}
            </div>
          )}
        </main>
      </div>

      {/* ── Menu de contexto ── */}
      {menu && (
        <div className="hx-menu" style={{ left: Math.min(menu.x, window.innerWidth - 190), top: Math.min(menu.y, window.innerHeight - 180) }}
          onClick={e => e.stopPropagation()}>
          <div className="hx-menu-title">{menu.p.name}</div>
          <button className="hx-mi" onClick={() => { setMenu(null); openProject(menu.p); }}>
            <window.Icon name="run" style="line" size={13} /> Abrir
          </button>
          <button className="hx-mi" onClick={() => { setMenu(null); setRenamingId(menu.p.id); }}>
            <window.Icon name="pen" style="line" size={13} /> Renomear
          </button>
          <button className="hx-mi" onClick={() => { setMenu(null); duplicate(menu.p); }}>
            <window.Icon name="copy" style="line" size={13} /> Duplicar
          </button>
          <div className="hx-menu-sep" />
          <button className="hx-mi danger" onClick={() => { setMenu(null); remove(menu.p); }}>
            <window.Icon name="trash" style="line" size={13} /> Excluir
          </button>
        </div>
      )}

      {/* ── Toast (com ação de desfazer) ── */}
      <div className={`hx-toast${toast ? ' show' : ''}`}>
        {toast && <>
          <span>{toast.msg}</span>
          {toast.action && <button className="hx-undo" onClick={toast.action.fn}>{toast.action.label}</button>}
        </>}
      </div>

      {modal && <CreateModal onClose={() => setModal(false)} onForge={forge} />}
      {profileOpen && <window.ProfileScreen onClose={() => setProfileOpen(false)} onOpenAch={() => setAchOpen(true)} />}
      {achOpen && <window.AchievementsScreen onClose={() => setAchOpen(false)} />}
      {launch && <LaunchOverlay info={launch} />}
    </div>
  );
}

ReactDOM.createRoot(document.getElementById('root')).render(<Launcher />);
