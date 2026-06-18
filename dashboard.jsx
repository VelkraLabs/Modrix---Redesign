/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Visão Geral do projeto (dashboard da oficina).
   Identidade editável (nome, versão, descrição), inventário do mod
   (tiles por seção), saúde do mod (diagnóstico leve) e ações rápidas.
   ───────────────────────────────────────────────────────────────────────── */

const DASH_SECTIONS = [
  { key: 'items',   label: 'Itens',       sprite: 'sword' },
  { key: 'blocks',  label: 'Blocos',      sprite: 'ore' },
  { key: 'tools',   label: 'Ferramentas', sprite: 'picareta' },
  { key: 'armor',   label: 'Armaduras',   sprite: 'peitoral' },
  { key: 'recipes', label: 'Receitas',    sprite: 'rubi' },
  { key: 'effects', label: 'Efeitos',     sprite: 'pocao' },
  { key: 'mobs',    label: 'Mobs',        sprite: 'golem' },
  { key: 'ores',    label: 'Minérios',    sprite: 'ore' },
  { key: 'trees',   label: 'Árvores',     sprite: 'folhas' },
];

/* diagnóstico leve do mod inteiro */
function dashHealth(mod) {
  const issues = [];
  for (const r of (mod.recipes || [])) {
    const errs = window.validateRecipe ? window.validateRecipe(r).filter(i => i.level === 'error') : [];
    if (errs.length) issues.push({ level: 'error', msg: `Receita “${r.name}”: ${errs[0].msg}`, go: ['recipes', r.id] });
  }
  const results = new Set((mod.recipes || []).map(r => r.result));
  for (const it of (mod.items || [])) {
    if (!results.has(it.id)) issues.push({ level: 'warn', msg: `“${it.name}” não tem receita — só seria obtido via comandos.`, go: ['items', it.id] });
  }
  const veined = new Set((mod.ores || []).map(o => o.block));
  for (const b of (mod.blocks || [])) {
    if (!veined.has(b.id)) issues.push({ level: 'warn', msg: `“${b.name}” não gera no mundo — crie um veio em Minérios.`, go: ['blocks', b.id] });
  }
  return issues.slice(0, 6);
}

/* tempo relativo curto */
function dashAgo(ts) {
  const s = (Date.now() - ts) / 1000;
  if (s < 60) return 'agora';
  if (s < 3600) return `há ${Math.floor(s / 60)} min`;
  if (s < 86400) return `há ${Math.floor(s / 3600)} h`;
  return `há ${Math.floor(s / 86400)} d`;
}

/* checklist de lançamento — cruzamento de conteúdo + conquistas + texturas */
function dashChecklist(mod) {
  const ach = window.mxAchUnlocked ? window.mxAchUnlocked() : new Set();
  let tex = {};
  try { tex = JSON.parse(localStorage.getItem('modrix_tex_v1') || '{}') || {}; } catch (e) {}
  const semReceita = (mod.items || []).some(it => !(mod.recipes || []).some(r => r.result === it.id));
  return [
    { label: 'Forjar o primeiro item',        done: (mod.items || []).length > 0 },
    { label: 'Criar um bloco',                done: (mod.blocks || []).length > 0 },
    { label: 'Dar receita a todos os itens',  done: (mod.items || []).length > 0 && !semReceita },
    { label: 'Gerar minério no mundo',        done: (mod.ores || []).length > 0 },
    { label: 'Pintar uma textura própria',    done: Object.keys(tex).length > 0 || ach.has('artist') },
    { label: 'Testar no jogo',                done: ach.has('tested') },
    { label: 'Publicar o .jar',               done: ach.has('published') },
  ];
}

function OverviewDashboard({ mod, setMeta, onGo, onCreate, onAction, onEditTexture, activity }) {
  const { useState: useDashState } = React;
  const [emblemPick, setEmblemPick] = useDashState(false);
  const issues = dashHealth(mod);
  const emblem = mod.emblem || (mod.items && mod.items[0] && mod.items[0].sprite) || 'sword';
  const allSprites = Object.keys(window.MX_SPRITES || {});
  const total = DASH_SECTIONS.reduce((s, x) => s + (mod[x.key] || []).length, 0);
  const checklist = dashChecklist(mod);
  const ckDone = checklist.filter(c => c.done).length;
  const ckPct = Math.round((ckDone / checklist.length) * 100);
  const ckFull = ckDone === checklist.length;
  const acts = activity || [];
  // vitrine: todos os registros com sprite (cap 24)
  const showcase = DASH_SECTIONS.flatMap(s => (mod[s.key] || []).map(r => ({ key: s.key, id: r.id, name: r.name, sprite: r.sprite }))).slice(0, 24);

  return (
    <div className="mx-main" data-screen-label="Visão geral do projeto">
      <div className="mx-breadcrumb">
        <span>{mod.modName}</span> <span>›</span> <b>Visão geral</b>
      </div>

      {/* ── Identidade ── */}
      <div className="mx-dash-head">
        <div style={{ position: 'relative', flexShrink: 0 }}>
          <button type="button" className="mx-dash-emblem pickable" title="Trocar o emblema do mod"
            onClick={() => setEmblemPick(o => !o)}>
            <window.PixelSprite sprite={emblem} size={64} />
            <span className="mx-dash-emblem-hint">trocar</span>
          </button>
          {onEditTexture && (
            <button type="button" className="mx-tex-edit" style={{ opacity: 1 }} title="Pintar o emblema"
              onClick={() => onEditTexture(emblem)}>
              <window.Icon name="pen" style="line" size={13} />
            </button>
          )}
          {emblemPick && (
            <div className="mx-emblem-pop" data-screen-label="Seletor de emblema">
              <div className="mx-emblem-pop-title">Emblema do mod</div>
              <div className="mx-emblem-grid">
                {allSprites.map(sp => (
                  <button type="button" key={sp} className={`mx-emblem-opt${sp === emblem ? ' on' : ''}`} title={sp}
                    onClick={() => { setMeta({ emblem: sp }); setEmblemPick(false); }}>
                    <window.PixelSprite sprite={sp} size={26} />
                  </button>
                ))}
              </div>
              <div className="mx-emblem-pop-hint">Aparece no launcher e na lista de mods.</div>
            </div>
          )}
        </div>
        <div className="mx-dash-id">
          <div className="mx-row">
            <window.TextField label="Nome do mod" value={mod.modName}
              onChange={v => setMeta({ modName: v })} placeholder="Joias de Aldoria"
              hint="Aparece no launcher e na lista de mods do jogo." />
            <window.TextField label="Versão" value={mod.modVersion} mono
              onChange={v => setMeta({ modVersion: v.replace(/[^0-9a-z.\-+]/gi, '') })}
              hint="semver — vira o sufixo do .jar." />
          </div>
          <div className="mx-row" style={{ marginTop: 14 }}>
            <window.TextField label="Autor" value={mod.author || ''}
              onChange={v => setMeta({ author: v })} placeholder="Seu nome ou apelido"
              hint="Créditos no mods.toml e na lista de mods." />
            <window.TextField label="ID do mod" value={mod.modId} mono
              onChange={v => setMeta({ modId: window.toId(v) })}
              hint={`namespace dos registros — ${mod.modId || '...'}:rubi`} />
          </div>
          <div style={{ marginTop: 14 }}>
            <label className="mx-label">Descrição</label>
            <textarea className="mx-field-input mx-dash-desc" rows={2} value={mod.description || ''}
              placeholder="O que o seu mod adiciona ao jogo?"
              onChange={e => setMeta({ description: e.target.value })} />
          </div>
          <div className="mx-dash-meta">
            <span className="mx-chip" title="Namespace atual dos registros">{mod.modId || '—'}</span>
            <span>NeoForge {mod.neoforge}</span>
            <span>·</span>
            <span style={{ fontFamily: 'var(--font-mono, monospace)' }}>{mod.folder}</span>
            {mod.author && <span>· por <b style={{ color: 'var(--text-muted)' }}>{mod.author}</b></span>}
            <span style={{ marginLeft: 'auto' }}>{total} registro{total === 1 ? '' : 's'}</span>
          </div>
        </div>
      </div>

      {/* ── Vitrine ── */}
      {showcase.length > 0 && (
        <>
          <div className="mx-dash-label">Vitrine</div>
          <div className="mx-dash-show">
            {showcase.map(s => (
              <button type="button" key={s.key + ':' + s.id} className="mx-dash-slot" title={s.name}
                onClick={() => onGo(s.key, s.id)}>
                <window.PixelSprite sprite={s.sprite} size={30} />
              </button>
            ))}
          </div>
        </>
      )}

      {/* ── Inventário do mod ── */}
      <div className="mx-dash-label">Inventário do mod</div>
      <div className="mx-dash-tiles">
        {DASH_SECTIONS.map(s => {
          const arr = mod[s.key] || [];
          const first = arr[0];
          return (
            <button type="button" key={s.key} className={`mx-dash-tile${arr.length === 0 ? ' empty' : ''}`}
              onClick={() => arr.length ? onGo(s.key, first.id) : onCreate(s.key, s.label)}
              title={arr.length ? `Abrir ${s.label.toLowerCase()}` : `Criar ${s.label.toLowerCase()}`}>
              <span className="mx-dash-tile-ico">
                <window.PixelSprite sprite={first ? first.sprite : s.sprite} size={30} />
              </span>
              <b>{arr.length}</b>
              <span className="mx-dash-tile-name">{s.label}</span>
              <span className="mx-dash-tile-go">{arr.length ? 'abrir →' : '+ criar'}</span>
            </button>
          );
        })}
      </div>

      <div className="mx-dash-cols">
        <div className="mx-dash-stack">
          {/* ── Saúde do mod ── */}
          <div className="mx-dash-card">
          <div className="mx-dash-label" style={{ margin: 0 }}>Saúde do mod</div>
          {issues.length === 0 ? (
            <div className="mx-dash-ok"><span>✓</span> Tudo certo — nenhum problema encontrado.</div>
          ) : (
            <div className="mx-dash-issues">
              {issues.map((it, i) => (
                <button type="button" key={i} className={`mx-dash-issue ${it.level}`} onClick={() => onGo(it.go[0], it.go[1])}>
                  <span className="mx-valid-dot" />
                  <span>{it.msg}</span>
                  <span className="mx-dash-issue-go">ver →</span>
                </button>
              ))}
            </div>
          )}
          </div>

          {/* ── Atividade recente ── */}
          <div className="mx-dash-card">
            <div className="mx-dash-label" style={{ margin: 0 }}>Atividade recente</div>
            {acts.length === 0 ? (
              <div className="mx-dash-act-empty">Suas ações aparecem aqui — forje algo!</div>
            ) : (
              <div className="mx-dash-act-list">
                {acts.map((a, i) => (
                  <div key={a.ts + ':' + i} className="mx-dash-act-row">
                    <span className="mx-dash-act-ico">
                      {a.sprite ? <window.PixelSprite sprite={a.sprite} size={18} /> : <window.Icon name="check" style="line" size={12} />}
                    </span>
                    <span className="mx-dash-act-msg">{a.msg}</span>
                    <span className="mx-dash-act-time">{dashAgo(a.ts)}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>

        <div className="mx-dash-stack">
          {/* ── Checklist de lançamento ── */}
          <div className={`mx-dash-card${ckFull ? ' gold' : ''}`}>
            <div className="mx-dash-label" style={{ margin: 0, display: 'flex', justifyContent: 'space-between' }}>
              <span>Rumo ao lançamento</span>
              <span style={{ letterSpacing: 0 }}>{ckDone}/{checklist.length}</span>
            </div>
            <div className="mx-dash-ck-bar"><i style={{ width: ckPct + '%' }} /></div>
            <div className="mx-dash-ck-list">
              {checklist.map((c, i) => (
                <div key={i} className={`mx-dash-ck${c.done ? ' done' : ''}`}>
                  <span className="mx-dash-ck-box">{c.done ? '✓' : ''}</span>
                  {c.label}
                </div>
              ))}
            </div>
            {ckFull && <div className="mx-dash-ck-full">✧ Mod pronto pro mundo — parabéns, ferreiro!</div>}
          </div>

          {/* ── Ações rápidas ── */}
          <div className="mx-dash-card">
          <div className="mx-dash-label" style={{ margin: 0 }}>Ações rápidas</div>
          <div className="mx-dash-actions">
            <button className="mx-dash-act pack" onClick={() => onAction('pack')}>
              <window.Icon name="grid" style="line" size={14} /> Adicionar conjunto pronto
            </button>
            <button className="mx-dash-act run" onClick={() => onAction('run')}>
              <window.Icon name="run" style="line" size={14} /> Testar no jogo
            </button>
            <button className="mx-dash-act" onClick={() => onAction('publish')}>
              <window.Icon name="spark" style="line" size={14} /> Publicar .jar
            </button>
            <button className="mx-dash-act" onClick={() => onAction('java')}>
              <window.Icon name="code" style="line" size={14} /> Ver código Java
            </button>
          </div>
          <div className="mx-dash-tip">
            <kbd className="mx-qs-kbd">Ctrl</kbd>+<kbd className="mx-qs-kbd">K</kbd> busca qualquer conteúdo ou ação.
          </div>
          </div>
        </div>
      </div>

      {/* ── Histórico de versões ── */}
      <div className="mx-dash-label">Histórico de versões</div>
      <div className="mx-dash-release">
        <div className="mx-dash-release-edit">
          <label className="mx-label">Notas da versão <code style={{ color: 'var(--accent)' }}>{mod.modVersion}</code></label>
          {(() => {
            const KEYS = ['items', 'blocks', 'tools', 'armor', 'recipes', 'effects', 'mobs', 'ores', 'trees'];
            const novos = KEYS.reduce((s, k) => s + (mod[k] || []).filter(r => r._new).length, 0);
            const last = (mod.changelog || [])[0];
            return novos > 0
              ? <div className="mx-dash-newline"><span className="mx-sec-newdot" /> <b>{novos}</b> {novos === 1 ? 'novidade' : 'novidades'}{last ? ` desde a v${last.version}` : ''} — pronta(s) para publicar.</div>
              : <div className="mx-dash-newline muted">Nenhuma mudança nova desde a última publicação.</div>;
          })()}
          <textarea className="mx-field-input mx-dash-desc" rows={2} value={mod.releaseNotes || ''}
            placeholder="O que mudou nesta versão? (vira a entrada do changelog ao publicar)"
            onChange={e => setMeta({ releaseNotes: e.target.value })} />
          <button type="button" className="mx-dash-act" style={{ marginTop: 10, width: 'fit-content' }} onClick={() => onAction('publish')}>
            <window.Icon name="spark" style="line" size={14} /> Publicar {mod.modVersion} → carimba a versão
          </button>
        </div>
        <div className="mx-dash-timeline">
          {(mod.changelog || []).length === 0 ? (
            <div className="mx-dash-act-empty">Nenhuma versão publicada ainda.</div>
          ) : (mod.changelog || []).map((e, i) => (
            <div key={e.version + ':' + i} className={`mx-dash-tl-row${i === 0 ? ' latest' : ''}`}>
              <span className="mx-dash-tl-dot" />
              <div className="mx-dash-tl-body">
                <div className="mx-dash-tl-head">
                  <span className="mx-dash-tl-ver">v{e.version}</span>
                  {i === 0 && <span className="mx-dash-tl-tag">{e.version === mod.modVersion ? 'atual' : 'última'}</span>}
                  <span className="mx-dash-tl-date">{e.date}</span>
                </div>
                <div className="mx-dash-tl-notes">{e.notes}</div>
                <div className="mx-dash-tl-counts">{e.items} itens · {e.blocks} blocos · {e.recipes} receitas</div>
              </div>
            </div>
          ))}
        </div>
      </div>
      <div style={{ height: 16 }} />
    </div>
  );
}

Object.assign(window, { OverviewDashboard });
