/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Quick Switcher (Ctrl+K), estilo inventário: busca tudo do mod
   (itens, blocos, ferramentas, armaduras, receitas, minérios) + ações
   rápidas (Testar, Publicar, Ver Java). ↑↓ navega, Enter abre, Esc fecha.
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useQsState, useEffect: useQsEffect, useRef: useQsRef } = React;

const QS_KIND_LABEL = {
  items: 'Item', blocks: 'Bloco', tools: 'Ferramenta',
  armor: 'Armadura', recipes: 'Receita', ores: 'Minério',
  effects: 'Efeito', mobs: 'Mob', trees: 'Árvore',
};

function QuickSwitcher({ mod, onPick, onAction, onClose }) {
  const [q, setQ] = useQsState('');
  const [sel, setSel] = useQsState(0);
  const listRef = useQsRef(null);

  // monta o índice: conteúdo + ações
  const all = [];
  for (const key of ['items', 'blocks', 'tools', 'armor', 'recipes', 'effects', 'mobs', 'ores', 'trees']) {
    for (const r of (mod[key] || [])) {
      all.push({ type: 'content', key, id: r.id, name: r.name, sub: `${QS_KIND_LABEL[key]} · ${mod.modId}:${r.id}`, sprite: r.sprite });
    }
  }
  all.push(
    { type: 'action', id: 'pack',    name: 'Adicionar conjunto pronto', sub: 'minério, ferramentas, armadura…', ico: 'grid' },
    { type: 'action', id: 'run',     name: 'Testar no jogo',  sub: 'compila e abre o Minecraft', ico: 'run' },
    { type: 'action', id: 'publish', name: 'Publicar mod',    sub: 'gera o .jar distribuível',   ico: 'spark' },
    { type: 'action', id: 'java',    name: 'Ver código Java', sub: 'arquivos gerados',           ico: 'code' },
  );

  const query = q.trim().toLowerCase();
  const results = query
    ? all.filter(e => e.name.toLowerCase().includes(query) || (e.sub || '').toLowerCase().includes(query))
    : all;
  const selIdx = Math.min(sel, Math.max(0, results.length - 1));

  useQsEffect(() => { setSel(0); }, [q]);
  useQsEffect(() => {
    const el = listRef.current && listRef.current.children[selIdx];
    if (el && el.offsetTop != null && listRef.current) {
      const top = el.offsetTop - listRef.current.offsetTop;
      const view = listRef.current;
      if (top < view.scrollTop) view.scrollTop = top;
      else if (top + el.offsetHeight > view.scrollTop + view.clientHeight) view.scrollTop = top + el.offsetHeight - view.clientHeight;
    }
  }, [selIdx]);

  function activate(e) {
    if (!e) return;
    if (e.type === 'content') onPick(e.key, e.id);
    else onAction(e.id);
    onClose();
  }

  function onKey(e) {
    if (e.key === 'ArrowDown') { e.preventDefault(); setSel(s => Math.min(s + 1, results.length - 1)); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); setSel(s => Math.max(s - 1, 0)); }
    else if (e.key === 'Enter') { e.preventDefault(); activate(results[selIdx]); }
    else if (e.key === 'Escape') { e.preventDefault(); onClose(); }
  }

  return (
    <div className="mx-veil" style={{ alignItems: 'flex-start', paddingTop: '12vh' }}
      onClick={e => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="mx-qs" data-screen-label="Quick Switcher">
        <div className="mx-qs-head">
          <window.Icon name="search" style="line" size={15} />
          <input autoFocus className="mx-qs-input" placeholder="Buscar conteúdo ou ação…"
            value={q} onChange={e => setQ(e.target.value)} onKeyDown={onKey} />
          <kbd className="mx-qs-kbd">esc</kbd>
        </div>
        <div className="mx-qs-list" ref={listRef}>
          {results.length === 0 && <div className="mx-qs-empty">Nada encontrado para “{q}”.</div>}
          {results.map((e, i) => (
            <div key={(e.key || e.type) + ':' + e.id} className={`mx-qs-row${i === selIdx ? ' on' : ''}`}
              onMouseEnter={() => setSel(i)} onClick={() => activate(e)}>
              <span className="mx-qs-ico">
                {e.sprite ? <window.PixelSprite sprite={e.sprite} size={22} /> : <window.Icon name={e.ico} style="line" size={15} />}
              </span>
              <span style={{ minWidth: 0, flex: 1 }}>
                <div className="mx-qs-name">{e.name}</div>
                <div className="mx-qs-sub">{e.sub}</div>
              </span>
              {e.type === 'action' && <span className="mx-qs-tag">ação</span>}
              {i === selIdx && <kbd className="mx-qs-kbd">↵</kbd>}
            </div>
          ))}
        </div>
        <div className="mx-qs-foot">
          <span><kbd className="mx-qs-kbd">↑↓</kbd> navegar</span>
          <span><kbd className="mx-qs-kbd">↵</kbd> abrir</span>
          <span style={{ marginLeft: 'auto', fontFamily: 'var(--font-mono, monospace)' }}>{results.length} resultado{results.length === 1 ? '' : 's'}</span>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { QuickSwitcher });
