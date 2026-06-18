/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Editor de Receitas + Preview ao vivo das estações de crafting.
   Grade "pintável": escolha um ingrediente no pincel e clique nas células.
   Tipos: Com forma · Sem forma · Fundição · Cortador · Forja.
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useRcState } = React;

const RC_TYPES = [
  { id: 'shaped',      label: 'Com forma' },
  { id: 'shapeless',   label: 'Sem forma' },
  { id: 'smelting',    label: 'Fundição' },
  { id: 'stonecutting',label: 'Cortador' },
  { id: 'smithing',    label: 'Forja' },
];
const RC_TYPE_LABEL = Object.fromEntries(RC_TYPES.map(t => [t.id, t.label]));

// ── helpers locais ─────────────────────────────────────────────────────────
function RcSection({ children, hint }) {
  return (
    <div style={{ fontSize: 12, fontWeight: 800, letterSpacing: '.8px', textTransform: 'uppercase',
      color: 'var(--text-dim)', margin: '24px 0 12px', paddingBottom: 8, borderBottom: '1px solid var(--border-sub)',
      display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', gap: 12 }}>
      <span>{children}</span>
      {hint && <span style={{ fontWeight: 500, letterSpacing: 0, textTransform: 'none', color: 'var(--text-dim)' }}>{hint}</span>}
    </div>
  );
}
function ingById(id) { return (window.MX_INGREDIENTS || []).find(i => i.id === id) || null; }
function regOf(id) { const i = ingById(id); return i ? i.reg : ('aldoria:' + (id || 'desconhecido')); }
function hasCount(type) { return type === 'shaped' || type === 'shapeless' || type === 'stonecutting'; }

function Arrow({ size = 30 }) {
  return (
    <svg width={size + 14} height={size} viewBox="0 0 44 30" style={{ display: 'block' }} aria-hidden="true">
      <path d="M2 15 H34 M26 6 L36 15 L26 24" fill="none" stroke="currentColor" strokeWidth="2.4"
        strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

// ── Validação ──────────────────────────────────────────────────────────────
function validateRecipe(rec) {
  const issues = [];
  const grid = rec.grid || [];
  const err = msg => issues.push({ level: 'error', msg });
  const warn = msg => issues.push({ level: 'warn', msg });

  if (!rec.id) err('Defina um ID de registro.');
  if (!rec.result) err('Escolha o item resultante.');
  if (hasCount(rec.type) && (!(Number(rec.count) >= 1))) err('A quantidade deve ser pelo menos 1.');

  if (rec.type === 'shaped' || rec.type === 'shapeless') {
    if (!grid.some(Boolean)) err('Adicione ao menos um ingrediente na grade.');
  } else if (rec.type === 'smelting' || rec.type === 'stonecutting') {
    if (!grid[0]) err(`Coloque o item de entrada ${rec.type === 'smelting' ? 'na fornalha' : 'no cortador'}.`);
    if (rec.type === 'smelting' && !(Number(rec.cookTime) >= 1)) warn('Tempo de cozimento muito baixo.');
  } else if (rec.type === 'smithing') {
    if (!grid[1]) err('Defina o item Base da forja.');
    if (!grid[2]) err('Defina a Adição da forja.');
    if (!grid[0]) warn('Sem molde — qualquer modelo será aceito.');
  }
  return issues;
}

// ── Gera o JSON de receita estilo NeoForge (datapack) ───────────────────────
function genRecipeJson(rec) {
  const grid = rec.grid || [];
  const count = Number(rec.count) || 1;
  if (rec.type === 'smelting') {
    return JSON.stringify({
      type: 'minecraft:smelting',
      ingredient: { item: regOf(grid[0]) },
      result: regOf(rec.result),
      experience: Number(rec.xp) || 0,
      cookingtime: Number(rec.cookTime) || 200,
    }, null, 2);
  }
  if (rec.type === 'stonecutting') {
    return JSON.stringify({
      type: 'minecraft:stonecutting',
      ingredient: { item: regOf(grid[0]) },
      result: { id: regOf(rec.result), count },
    }, null, 2);
  }
  if (rec.type === 'smithing') {
    return JSON.stringify({
      type: 'minecraft:smithing_transform',
      template: { item: regOf(grid[0]) },
      base: { item: regOf(grid[1]) },
      addition: { item: regOf(grid[2]) },
      result: { id: regOf(rec.result) },
    }, null, 2);
  }
  if (rec.type === 'shapeless') {
    return JSON.stringify({
      type: 'minecraft:crafting_shapeless',
      ingredients: grid.filter(Boolean).map(id => ({ item: regOf(id) })),
      result: { id: regOf(rec.result), count },
    }, null, 2);
  }
  // shaped — atribui símbolos e recorta linhas/colunas vazias
  const rows = [grid.slice(0, 3), grid.slice(3, 6), grid.slice(6, 9)];
  const SYM = 'ABCDEFGHI'; const idToSym = {}; let next = 0;
  rows.forEach(r => r.forEach(c => { if (c && !(c in idToSym)) idToSym[c] = SYM[next++]; }));
  let pattern = rows.map(r => r.map(c => (c ? idToSym[c] : ' ')).join(''));
  while (pattern.length > 1 && pattern[0].trim() === '') pattern.shift();
  while (pattern.length > 1 && pattern[pattern.length - 1].trim() === '') pattern.pop();
  const colEmpty = c => pattern.every(r => (r[c] || ' ') === ' ');
  while (pattern[0].length > 1 && colEmpty(0)) pattern = pattern.map(r => r.slice(1));
  while (pattern[0].length > 1 && colEmpty(pattern[0].length - 1)) pattern = pattern.map(r => r.slice(0, -1));
  const key = {};
  Object.entries(idToSym).forEach(([id, sym]) => { key[sym] = { item: regOf(id) }; });
  return JSON.stringify({ type: 'minecraft:crafting_shaped', pattern, key, result: { id: regOf(rec.result), count } }, null, 2);
}

// ═══════════════════════════════════════════════════════════════════════════
//  EDITOR (painel central)
// ═══════════════════════════════════════════════════════════════════════════
function RecipeEditor({ rec, modId, set }) {
  const ING = window.MX_INGREDIENTS || [];
  const [brush, setBrush] = useRcState(ING[0] ? ING[0].id : '');
  const grid = rec.grid || Array(9).fill(null);
  const t = rec.type;
  const resultIng = ingById(rec.result);
  const issues = validateRecipe(rec);

  function paint(i) { const g = grid.slice(); g[i] = brush || null; set({ grid: g }); }
  function clearCell(i, e) { e.preventDefault(); const g = grid.slice(); g[i] = null; set({ grid: g }); }

  function cell(i, sz, extraCls) {
    const c = grid[i]; const ing = c ? ingById(c) : null;
    return (
      <div className={`mx-cslot${ing ? ' filled' : ''}${extraCls ? ' ' + extraCls : ''}`} onClick={() => paint(i)}
        onContextMenu={e => clearCell(i, e)} title={ing ? ing.name : 'Clique p/ preencher · botão direito limpa'}>
        {ing && <window.PixelSprite sprite={ing.sprite} size={sz || 40} />}
      </div>
    );
  }
  function labeledSlot(i, label) {
    return (
      <div className="mx-smith-slot" key={i}>
        {cell(i, 38)}
        <span className="mx-slot-label">{label}</span>
      </div>
    );
  }

  // área de entrada por tipo
  let inputArea;
  if (t === 'smelting') {
    inputArea = (
      <div className="mx-furnace">
        <div className="mx-fcol">
          {cell(0, 42)}
          <div className="mx-flame"><i /></div>
          <div className="mx-cslot fuel" title="Combustível (carvão, lava…)"><span className="mx-fuel-glow" /></div>
        </div>
      </div>
    );
  } else if (t === 'stonecutting') {
    inputArea = (
      <div className="mx-cut">
        <div className="mx-smith-slot">{cell(0, 42)}<span className="mx-slot-label">entrada</span></div>
        <span className="mx-saw" title="Cortador de pedra"><window.Icon name="grid" style="line" size={20} /></span>
      </div>
    );
  } else if (t === 'smithing') {
    inputArea = (
      <div className="mx-smith">
        {labeledSlot(0, 'Modelo')}
        {labeledSlot(1, 'Base')}
        {labeledSlot(2, 'Adição')}
      </div>
    );
  } else {
    inputArea = <div className="mx-cgrid">{[0, 1, 2, 3, 4, 5, 6, 7, 8].map(i => <React.Fragment key={i}>{cell(i)}</React.Fragment>)}</div>;
  }

  const benchHint = t === 'smelting' ? 'minério → lingote/gema'
    : t === 'stonecutting' ? 'corte 1 → 1'
    : t === 'smithing' ? 'modelo + base + adição'
    : t === 'shapeless' ? 'a posição não importa' : 'a forma importa';
  const benchTitle = t === 'smelting' ? 'Fornalha' : t === 'stonecutting' ? 'Cortador de pedra'
    : t === 'smithing' ? 'Bancada de ferreiro' : 'Bancada de trabalho';

  return (
    <div>
      {/* ── Cabeçalho ── */}
      <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start' }}>
        <div>
          <label className="mx-label">Resultado</label>
          <div className="mx-tex-pick filled" style={{ cursor: 'default' }} title="Definido em “Resultado” abaixo">
            {resultIng ? <window.PixelSprite sprite={resultIng.sprite} size={58} /> : <span style={{ color: 'var(--text-dim)', fontSize: 11 }}>—</span>}
            {hasCount(t) && (Number(rec.count) || 1) > 1 && <span className="mx-tex-count">×{rec.count}</span>}
          </div>
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ marginBottom: 18 }}>
            <window.TextField label="Nome da receita" value={rec.name}
              onChange={v => set({ name: v, id: rec._idEdited ? rec.id : window.toId(v) })}
              placeholder="Espada de Rubi" hint="Rótulo no livro de receitas." />
          </div>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: window.toId(v), _idEdited: true })} hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      {/* ── Tipo ── */}
      <RcSection>Tipo de receita</RcSection>
      <window.Segmented value={t} options={RC_TYPES} onChange={v => set({ type: v })} />

      {/* ── Estação ── */}
      <RcSection hint={benchHint}>{benchTitle}</RcSection>
      <div className="mx-bench">
        {inputArea}
        <div className="mx-carrow"><Arrow /></div>
        <div className="mx-result-slot" title={resultIng ? resultIng.name : 'sem resultado'}>
          {resultIng && <window.PixelSprite sprite={resultIng.sprite} size={48} />}
          {hasCount(t) && (Number(rec.count) || 1) > 1 && <span className="mx-cbadge">{rec.count}</span>}
        </div>
      </div>

      {/* ── Paleta de ingredientes ── */}
      <div className="mx-palette-head">
        <span className="mx-label" style={{ margin: 0 }}>Pincel de ingredientes</span>
        <span style={{ fontSize: 11, color: 'var(--text-dim)' }}>clique numa célula para aplicar</span>
      </div>
      <div className="mx-palette">
        <button type="button" className={`mx-pchip eraser${brush === '' ? ' on' : ''}`} onClick={() => setBrush('')}>
          <span className="mx-pchip-ico"><window.Icon name="check" style="line" size={13} /></span>
          Borracha
        </button>
        {ING.map(ig => (
          <button type="button" key={ig.id} className={`mx-pchip${brush === ig.id ? ' on' : ''}`} onClick={() => setBrush(ig.id)}>
            <span className="mx-pchip-ico"><window.PixelSprite sprite={ig.sprite} size={20} /></span>
            {ig.name}
          </button>
        ))}
      </div>

      {/* ── Resultado ── */}
      <RcSection>Resultado</RcSection>
      <div className="mx-row">
        <window.SelectField label="Item resultante" value={rec.result}
          options={ING.map(ig => ({ id: ig.id, label: ig.name }))}
          onChange={v => { const ng = ingById(v); set({ result: v, sprite: ng ? ng.sprite : rec.sprite }); }} />
        {hasCount(t)
          ? <window.NumberField label="Quantidade" value={rec.count} min={1} max={64} onChange={v => set({ count: v })} hint="Itens produzidos por uso." />
          : <div style={{ display: 'flex', alignItems: 'flex-end' }}><div className="mx-fixed-note">Produz sempre 1 unidade.</div></div>}
      </div>

      {/* ── Parâmetros de fundição ── */}
      {t === 'smelting' && (
        <>
          <RcSection>Parâmetros da fornalha</RcSection>
          <div className="mx-row">
            <window.NumberField label="Tempo de cozimento" value={rec.cookTime} min={20} step={20}
              onChange={v => set({ cookTime: v })} hint="Em ticks. 200 = 10s na fornalha." />
            <window.NumberField label="XP concedido" value={rec.xp} min={0} step={0.1}
              onChange={v => set({ xp: v })} hint="Experiência ao coletar." />
          </div>
        </>
      )}

      {/* ── Validação ── */}
      {issues.length > 0 && (
        <div className="mx-valid">
          {issues.map((it, i) => (
            <div key={i} className={`mx-valid-row ${it.level}`}>
              <span className="mx-valid-dot" />{it.msg}
            </div>
          ))}
        </div>
      )}
      <div style={{ height: 16 }} />
    </div>
  );
}

// ═══════════════════════════════════════════════════════════════════════════
//  PREVIEW (painel direito)
// ═══════════════════════════════════════════════════════════════════════════
function RecipePreview({ rec, modId, popKey, fxKey, gameFeel }) {
  const t = rec.type;
  const stationLabel = t === 'smelting' ? 'Fornalha' : t === 'stonecutting' ? 'Cortador'
    : t === 'smithing' ? 'Forja' : 'Bancada';
  const tabs = [{ id: 'bench', label: stationLabel }, { id: 'json', label: 'JSON' }];
  const [tab, setTab] = useRcState('bench');
  const grid = rec.grid || [];
  const resultIng = ingById(rec.result);
  const count = Number(rec.count) || 1;

  const [parts, setParts] = useRcState([]);
  const firstFx = React.useRef(true);
  React.useEffect(() => {
    if (firstFx.current) { firstFx.current = false; return; }
    if (!gameFeel || !fxKey) return;
    const colors = ['#ff7d97', '#e1395b', '#f5c46a', 'var(--accent)', '#ffffff'];
    const ps = Array.from({ length: 16 }, (_, i) => ({
      id: `${fxKey}-${i}`, bg: colors[i % colors.length],
      dx: `${Math.round((Math.random() * 2 - 1) * 130)}px`,
      dy: `${Math.round((Math.random() * 2 - 1) * 110 - 25)}px`,
      delay: `${Math.round(Math.random() * 90)}ms`, size: 4 + Math.round(Math.random() * 5),
    }));
    setParts(ps);
    const to = setTimeout(() => setParts([]), 900);
    return () => clearTimeout(to);
  }, [fxKey, gameFeel]);

  function gslot(i, extra) {
    const c = grid[i]; const ing = c ? ingById(c) : null;
    return <div key={i} className={`mx-gslot${extra ? ' ' + extra : ''}`}>{ing && <window.PixelSprite sprite={ing.sprite} size={34} />}</div>;
  }

  let stationView;
  if (t === 'smelting') {
    stationView = (
      <div className="mx-furnace-pv"><div className="mx-fcol">
        {gslot(0)}
        <div className="mx-flame"><i /></div>
        <div className="mx-gslot fuel"><span className="mx-fuel-glow" /></div>
      </div></div>
    );
  } else if (t === 'stonecutting') {
    stationView = <div className="mx-cut-pv">{gslot(0)}</div>;
  } else if (t === 'smithing') {
    stationView = <div className="mx-smith-pv">{gslot(0)}{gslot(1)}{gslot(2)}</div>;
  } else {
    stationView = <div className="mx-ggrid">{[0, 1, 2, 3, 4, 5, 6, 7, 8].map(i => gslot(i))}</div>;
  }

  const distinct = [...new Set(grid.filter(Boolean))].length;

  return (
    <aside className="mx-preview">
      <div className="mx-pv-head">
        <span className="mx-pv-title"><span className="mx-live-dot" /> Preview ao vivo</span>
        <span style={{ fontSize: 11, color: 'var(--text-dim)', fontFamily: 'var(--font-mono, monospace)' }}>receita</span>
      </div>

      <div className="mx-pv-tabs">
        <window.Segmented options={tabs} value={tab} onChange={setTab} />
      </div>

      <div className="mx-pv-body">
        {tab === 'bench' && (
          <div className="mx-stage">
            {parts.map(p => (
              <span key={p.id} className="mx-particle"
                style={{ background: p.bg, width: p.size, height: p.size, '--dx': p.dx, '--dy': p.dy, animationDelay: p.delay }} />
            ))}
            <div key={popKey} className="mx-pop mx-ctable">
              {stationView}
              <span className="mx-carrow"><Arrow size={26} /></span>
              <div className="mx-gresult">
                {resultIng && <window.PixelSprite sprite={resultIng.sprite} size={44} />}
                {hasCount(t) && count > 1 && <span className="mx-cbadge">{count}</span>}
              </div>
            </div>
          </div>
        )}

        {tab === 'json' && (
          <div className="mx-json-wrap">
            <div className="mx-json-bar">
              <span style={{ fontFamily: 'var(--font-mono, monospace)' }}>data/{modId}/recipe/{rec.id || '...'}.json</span>
              <span className="mx-json-tag">NeoForge</span>
            </div>
            <pre className="mx-json">{genRecipeJson(rec)}</pre>
          </div>
        )}

        <div className="mx-pv-meta">
          <div className="mx-meta-card">
            <div className="mx-meta-row"><span className="mx-meta-k">Estação</span>
              <span className="mx-meta-v" style={{ color: 'var(--accent)' }}>{RC_TYPE_LABEL[t] || t}</span></div>
            <div className="mx-meta-row"><span className="mx-meta-k">Produz</span>
              <span className="mx-meta-v">{resultIng ? resultIng.name : '—'}{hasCount(t) && count > 1 ? ` ×${count}` : ''}</span></div>
            {t === 'smelting' && <div className="mx-meta-row"><span className="mx-meta-k">Cozimento</span><span className="mx-meta-v">{rec.cookTime}t · {rec.xp} XP</span></div>}
            {t === 'smithing' && <div className="mx-meta-row"><span className="mx-meta-k">Base + Adição</span><span className="mx-meta-v">{(ingById(grid[1]) || {}).name || '—'} + {(ingById(grid[2]) || {}).name || '—'}</span></div>}
            {(t === 'shaped' || t === 'shapeless') && <div className="mx-meta-row"><span className="mx-meta-k">Ingredientes</span><span className="mx-meta-v">{grid.filter(Boolean).length} · {distinct} tipo{distinct === 1 ? '' : 's'}</span></div>}
            {t === 'stonecutting' && <div className="mx-meta-row"><span className="mx-meta-k">Entrada</span><span className="mx-meta-v">{(ingById(grid[0]) || {}).name || '—'}</span></div>}
          </div>
          <div style={{ fontSize: 11, color: 'var(--text-dim)', textAlign: 'center', lineHeight: 1.5 }}>
            Pinte os slots à esquerda — o resultado se monta aqui. Nada é salvo até clicar em <b style={{ color: 'var(--text-muted)' }}>Salvar</b>.
          </div>
        </div>
      </div>
    </aside>
  );
}

Object.assign(window, { RecipeEditor, RecipePreview, genRecipeJson, validateRecipe });
