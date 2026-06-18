/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Mapa do mod: grafo VIVO derivado do conteúdo real do projeto.
   Nós = cada registro (item, bloco, ferramenta, armadura, minério, mob,
   efeito, árvore, receita) + "materiais" referenciados mas ainda sem editor
   (ex.: o rubi bruto que a fundição produz). Arestas = relações reais lidas
   dos campos: minério→bloco (gera), bloco/mob→item (dropa), ingrediente→
   receita (×N) e receita→resultado (produz).
   Layout automático em camadas (longest-path) — packs viram cadeias sozinhos.
   Pan por arraste, zoom por botões, hover destaca conexões, clique navega.
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useMmState, useRef: useMmRef, useEffect: useMmEffect, useMemo: useMmMemo } = React;

const MM_NODE_W = 184, MM_NODE_H = 58;
const MM_COL_W = 256, MM_ROW_H = 90, MM_PAD = 56;

const MM_RAR = { COMMON: 'Comum', UNCOMMON: 'Incomum', RARE: 'Raro', EPIC: 'Épico' };
const MM_RTYPE = {
  shaped: 'Bancada 3×3', shapeless: 'Bancada livre', smelting: 'Fornalha',
  blasting: 'Alto-forno', smoking: 'Defumador', stonecutting: 'Cortador',
  smithing: 'Bancada de ferreiro', campfire: 'Fogueira',
};
const MM_KIND_ICO = {
  item: 'cube', block: 'brick', tool: 'pick', armor: 'shield', ore: 'ore',
  mob: 'mob', effect: 'spark', tree: 'tree', recipe: 'grid', material: 'spark',
};

// ── Constrói nós + arestas a partir do mod ──────────────────────────────────
function mmBuildGraph(mod) {
  const nodes = [];
  const byRecord = {};                  // id → nó (só itens/blocos/ferramentas/armaduras)
  const push = n => { nodes.push(n); return n; };

  const sections = [
    ['ores', 'ore'], ['mobs', 'mob'], ['blocks', 'block'], ['items', 'item'],
    ['tools', 'tool'], ['armor', 'armor'], ['recipes', 'recipe'],
    ['effects', 'effect'], ['trees', 'tree'],
  ];
  sections.forEach(([key, kind]) => {
    (mod[key] || []).forEach(r => {
      let type;
      if (kind === 'item') type = r.consumable === 'food' ? 'Item · Comida' : ('Item' + (MM_RAR[r.rarity] ? ' · ' + MM_RAR[r.rarity] : ''));
      else if (kind === 'armor') type = 'Armadura' + (MM_RAR[r.rarity] ? ' · ' + MM_RAR[r.rarity] : '');
      else if (kind === 'block') type = 'Bloco';
      else if (kind === 'tool') type = 'Ferramenta';
      else if (kind === 'ore') type = 'Geração de minério';
      else if (kind === 'mob') type = 'Mob';
      else if (kind === 'effect') type = 'Efeito de status';
      else if (kind === 'tree') type = 'Árvore';
      else type = 'Receita · ' + (MM_RTYPE[r.type] || r.type);
      const node = push({ key: key + ':' + r.id, kind, label: r.name, type, sprite: r.sprite, nav: { key, id: r.id } });
      if (key === 'items' || key === 'blocks' || key === 'tools' || key === 'armor') byRecord[r.id] = node;
    });
  });

  // materiais referenciados mas sem registro próprio (ex.: rubi bruto da forja)
  const ING = window.MX_INGREDIENTS || [];
  const modNs = (mod.modId || '') + ':';
  const mats = {};
  const MM_TAGLBL = { material: 'Material', item: 'Item referenciado', bloco: 'Bloco referenciado' };
  function resolveThing(refId) {
    if (!refId) return null;
    if (byRecord[refId]) return byRecord[refId];
    const ing = ING.find(g => g.id === refId);
    if (ing && ing.reg && ing.reg.indexOf(modNs) === 0) {
      if (!mats[refId]) mats[refId] = push({ key: 'mat:' + refId, kind: 'material', label: ing.name, type: MM_TAGLBL[ing.tag] || 'Material', sprite: ing.sprite, stub: true, nav: null });
      return mats[refId];
    }
    return null;                        // item base do Minecraft → não polui o mapa
  }

  const edges = [];
  const add = (from, to, label) => { if (from && to && from.key !== to.key) edges.push({ from: from.key, to: to.key, label }); };

  (mod.ores || []).forEach(o => {
    const b = byRecord[o.block];
    if (b && b.kind === 'block') add({ key: 'ores:' + o.id }, b, 'gera');
  });
  (mod.blocks || []).forEach(b => add({ key: 'blocks:' + b.id }, resolveThing(b.drops), 'dropa'));
  (mod.mobs || []).forEach(m => add({ key: 'mobs:' + m.id }, resolveThing(m.dropItem), 'dropa'));
  (mod.recipes || []).forEach(r => {
    const rn = { key: 'recipes:' + r.id };
    const counts = {};
    (r.grid || []).forEach(c => { if (c) counts[c] = (counts[c] || 0) + 1; });
    Object.keys(counts).forEach(id => add(resolveThing(id), rn, counts[id] > 1 ? '×' + counts[id] : 'ingrediente'));
    add(rn, resolveThing(r.result), 'produz');
  });

  const dim = mmLayout(nodes, edges);
  return { nodes, edges, ...dim };
}

// ── Layout em camadas (longest-path) + lane de nós sem conexão ──────────────
function mmLayout(nodes, edges) {
  const byKey = Object.fromEntries(nodes.map(n => [n.key, n]));
  const inc = {}; nodes.forEach(n => { inc[n.key] = []; });
  const E = []; const seen = new Set();
  edges.forEach(e => {
    if (!byKey[e.from] || !byKey[e.to]) return;
    const k = e.from + '>' + e.to + '>' + e.label; if (seen.has(k)) return; seen.add(k);
    E.push(e); inc[e.to].push(e.from);
  });
  const connected = new Set(); E.forEach(e => { connected.add(e.from); connected.add(e.to); });
  const flow = nodes.filter(n => connected.has(n.key));
  const orphans = nodes.filter(n => !connected.has(n.key));

  const layer = {}; flow.forEach(n => { layer[n.key] = 0; });
  for (let it = 0; it < flow.length + 2; it++) {
    let ch = false;
    E.forEach(e => { if (layer[e.to] < layer[e.from] + 1) { layer[e.to] = layer[e.from] + 1; ch = true; } });
    if (!ch) break;
  }
  const maxL = flow.reduce((m, n) => Math.max(m, layer[n.key]), 0);
  const layers = []; for (let l = 0; l <= maxL; l++) layers.push(flow.filter(n => layer[n.key] === l));
  const maxCount = layers.reduce((m, l) => Math.max(m, l.length), 1);
  const flowH = maxCount * MM_ROW_H;

  function bary(n) {
    const ins = inc[n.key]; if (!ins.length) return 1e9;
    let s = 0, c = 0; ins.forEach(k => { const p = byKey[k]; if (p && typeof p.y === 'number') { s += p.y; c++; } });
    return c ? s / c : 1e9;
  }
  layers.forEach((lay, l) => {
    if (l > 0) lay.sort((a, b) => bary(a) - bary(b));
    const startY = (flowH - lay.length * MM_ROW_H) / 2;
    lay.forEach((n, i) => { n.x = MM_PAD + l * MM_COL_W; n.y = MM_PAD + startY + i * MM_ROW_H; });
  });

  let orphanTop = null;
  if (orphans.length) {
    orphanTop = MM_PAD + flowH + 56;
    const cols = Math.max(3, Math.min(6, orphans.length));
    orphans.forEach((n, i) => { n.x = MM_PAD + (i % cols) * MM_COL_W; n.y = orphanTop + 30 + Math.floor(i / cols) * MM_ROW_H; n.orphan = true; });
  }

  let mx = 0, my = 0;
  nodes.forEach(n => { mx = Math.max(mx, (n.x || 0) + MM_NODE_W); my = Math.max(my, (n.y || 0) + MM_NODE_H); });
  return { W: mx + MM_PAD, H: my + MM_PAD, orphanTop };
}

function mmAnchor(n) {
  return { out: { x: n.x + MM_NODE_W, y: n.y + MM_NODE_H / 2 }, inn: { x: n.x, y: n.y + MM_NODE_H / 2 } };
}

function ModMap({ mod, iconStyle, onOpen, onStub }) {
  const wrapRef = useMmRef(null);
  const [view, setView] = useMmState({ x: 0, y: 0, s: 1 });
  const [panning, setPanning] = useMmState(false);
  const [hover, setHover] = useMmState(null);
  const viewRef = useMmRef(view); viewRef.current = view;

  const g = useMmMemo(() => mmBuildGraph(mod), [mod]);
  const byId = useMmMemo(() => Object.fromEntries(g.nodes.map(n => [n.key, n])), [g]);

  function fit() {
    const el = wrapRef.current; if (!el) return;
    const r = el.getBoundingClientRect();
    const s = Math.min(r.width / g.W, r.height / g.H) * 0.92;
    setView({ s: Math.min(s, 1.4), x: (r.width - g.W * s) / 2, y: (r.height - g.H * s) / 2 });
  }
  useMmEffect(() => { fit(); }, [g.W, g.H]);

  function zoom(f) {
    const el = wrapRef.current; const r = el.getBoundingClientRect();
    const v = viewRef.current;
    const s = Math.max(0.35, Math.min(1.8, v.s * f));
    const cx = r.width / 2, cy = r.height / 2;
    setView({ s, x: cx - ((cx - v.x) / v.s) * s, y: cy - ((cy - v.y) / v.s) * s });
  }

  function onPointerDown(e) {
    if (e.target.closest('.mxm-node') || e.target.closest('.mxm-ctrl')) return;
    const start = { sx: e.clientX, sy: e.clientY, ox: viewRef.current.x, oy: viewRef.current.y };
    setPanning(true);
    const move = ev => setView(v => ({ ...v, x: start.ox + (ev.clientX - start.sx), y: start.oy + (ev.clientY - start.sy) }));
    const up = () => { setPanning(false); window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); };
    window.addEventListener('pointermove', move); window.addEventListener('pointerup', up);
  }

  const isHotEdge = e => hover && (e.from === hover || e.to === hover);
  const isDimNode = n => hover && n.key !== hover && !g.edges.some(e => isHotEdge(e) && (e.from === n.key || e.to === n.key));

  if (!g.nodes.length) {
    return (
      <div ref={wrapRef} className="mxm-wrap">
        <div className="mx-empty-state">
          <div className="mx-empty-slot"><window.Icon name="grid" style={iconStyle} size={34} /></div>
          <h2>Nada para mapear ainda</h2>
          <p>O mapa se desenha sozinho conforme você cria conteúdo. Adicione um item, um minério ou um <b>conjunto pronto</b> e veja as relações aparecerem.</p>
        </div>
      </div>
    );
  }

  return (
    <div ref={wrapRef} className={`mxm-wrap${panning ? ' panning' : ''}`} onPointerDown={onPointerDown}>
      <div className="mxm-world" style={{ transform: `translate(${view.x}px, ${view.y}px) scale(${view.s})`, width: g.W, height: g.H }}>
        {/* arestas */}
        <svg width={g.W} height={g.H} style={{ position: 'absolute', inset: 0, overflow: 'visible' }}>
          <defs>
            <marker id="mm-arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
              <path d="M0 0 L8 4 L0 8 z" fill="currentColor" />
            </marker>
          </defs>
          {g.edges.map((e, i) => {
            const A = byId[e.from], B = byId[e.to]; if (!A || !B) return null;
            const a = mmAnchor(A).out, b = mmAnchor(B).inn;
            const mx = (a.x + b.x) / 2;
            const hot = isHotEdge(e);
            const dim = hover && !hot;
            return (
              <path key={i} className={`mxm-edge${hot ? ' hot' : ''}${dim ? ' dim' : ''}`}
                d={`M ${a.x} ${a.y} C ${mx} ${a.y}, ${mx} ${b.y}, ${b.x} ${b.y}`}
                markerEnd="url(#mm-arrow)" />
            );
          })}
        </svg>
        {/* rótulos das arestas */}
        {g.edges.map((e, i) => {
          const A = byId[e.from], B = byId[e.to]; if (!A || !B) return null;
          const a = mmAnchor(A).out, b = mmAnchor(B).inn;
          const hot = isHotEdge(e);
          return (
            <span key={i} className={`mxm-elabel${hot ? ' hot' : ''}${hover && !hot ? ' dim' : ''}`}
              style={{ left: (a.x + b.x) / 2, top: (a.y + b.y) / 2 }}>{e.label}</span>
          );
        })}
        {/* divisória dos avulsos */}
        {g.orphanTop != null && (
          <div className="mxm-orphan-label" style={{ left: MM_PAD, top: g.orphanTop }}>
            sem conexões ainda — itens, peças e mecânicas isoladas
          </div>
        )}
        {/* nós */}
        {g.nodes.map(n => (
          <div key={n.key}
            className={`mxm-node${hover === n.key ? ' hot' : ''}${isDimNode(n) ? ' dim' : ''}${n.stub ? ' stub' : ''}`}
            style={{ left: n.x, top: n.y }}
            onMouseEnter={() => setHover(n.key)} onMouseLeave={() => setHover(null)}
            onClick={() => n.nav ? onOpen(n.nav.key, n.nav.id) : onStub(n.label)}>
            <span className="mxm-ico">
              {n.sprite ? <window.PixelSprite sprite={n.sprite} size={26} />
                : <window.Icon name={MM_KIND_ICO[n.kind] || 'spark'} style={iconStyle} size={17} />}
            </span>
            <span style={{ minWidth: 0 }}>
              <div className="mxm-name">{n.label}</div>
              <div className="mxm-type">{n.type}</div>
            </span>
          </div>
        ))}
      </div>

      {/* controles */}
      <div className="mxm-ctrl">
        <button onClick={() => zoom(1.25)} title="Aproximar">+</button>
        <button onClick={() => zoom(0.8)} title="Afastar">−</button>
        <button onClick={fit} title="Ajustar à tela" style={{ fontSize: 11 }}>⊡</button>
      </div>

      <div className="mxm-title-card">
        <div style={{ fontSize: 13, fontWeight: 800 }}>Mapa do mod</div>
        <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>{g.nodes.length} registros · como os conteúdos se conectam</div>
      </div>

      <div className="mxm-hint">arraste para navegar · passe o mouse para ver conexões · clique num nó para editar</div>
    </div>
  );
}

Object.assign(window, { ModMap });
