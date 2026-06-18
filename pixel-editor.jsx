/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Pixel editor de texturas (16×16).
   Edita o sprite compartilhado: o override vai pra window.MX_TEX_OVERRIDES,
   persiste em localStorage e o evento 'mx-tex' redesenha todos os canvases.
   Ferramentas: pincel · borracha · balde · conta-gotas · desfazer.
   ───────────────────────────────────────────────────────────────────────── */
const { useState: usePxState, useEffect: usePxEffect, useRef: usePxRef } = React;

/* overrides persistidos */
window.MX_TEX_OVERRIDES = (() => {
  try { return JSON.parse(localStorage.getItem('modrix_tex_v1') || '{}') || {}; } catch (e) { return {}; }
})();

function pxPersist() {
  try { localStorage.setItem('modrix_tex_v1', JSON.stringify(window.MX_TEX_OVERRIDES)); } catch (e) {}
}

/* sprite (rows+palette) → grade de 256 cores (hex|null) */
function pxToGrid(sprite) {
  const ov = window.MX_TEX_OVERRIDES[sprite];
  const rows = ov ? ov.rows : window.MX_SPRITES[sprite];
  const pal = ov ? ov.palette : window.MX_PALETTES[sprite];
  const g = new Array(256).fill(null);
  if (!rows) return g;
  for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) {
    g[y * 16 + x] = (pal && pal[(rows[y] || '')[x]]) || null;
  }
  return g;
}

/* grade → rows+palette (chars a..z) */
function pxFromGrid(grid) {
  const colors = [...new Set(grid.filter(Boolean))];
  const CH = 'abcdefghijklmnopqrstuvwxyz';
  const map = {}; const palette = { '.': null };
  colors.slice(0, 26).forEach((c, i) => { map[c] = CH[i]; palette[CH[i]] = c; });
  const rows = [];
  for (let y = 0; y < 16; y++) {
    let r = '';
    for (let x = 0; x < 16; x++) { const c = grid[y * 16 + x]; r += c && map[c] ? map[c] : '.'; }
    rows.push(r);
  }
  return { rows, palette };
}

const PX_COLORS = [
  '#ffffff', '#d6dde8', '#9aa1ad', '#5b626e', '#30343c', '#101216',
  '#ff8fa3', '#e23b5b', '#9c1f3a', '#e8a25c', '#d97a1e', '#8a5a1e',
  '#f5d76e', '#cbb277', '#b9925a', '#8a6437', '#6e4a26', '#4a3318',
  '#9ee37d', '#5fae4a', '#2e7d32', '#7eb8ff', '#3a78d8', '#2a4a8a',
  '#b08aff', '#7a4fd0', '#2dd4bf', '#1a8a7d',
];

const PX_TOOLS = [
  { id: 'paint', label: 'Pincel',      ico: 'pen' },
  { id: 'erase', label: 'Borracha',    ico: 'check' },
  { id: 'fill',  label: 'Balde',       ico: 'cube' },
  { id: 'pick',  label: 'Conta-gotas', ico: 'search' },
];

function pxFlood(grid, idx, to) {
  const from = grid[idx];
  if (from === to) return grid;
  const g = grid.slice();
  const stack = [idx];
  while (stack.length) {
    const i = stack.pop();
    if (g[i] !== from) continue;
    g[i] = to;
    const x = i % 16, y = (i / 16) | 0;
    if (x > 0) stack.push(i - 1);
    if (x < 15) stack.push(i + 1);
    if (y > 0) stack.push(i - 16);
    if (y < 15) stack.push(i + 16);
  }
  return g;
}

/* mini-canvas que desenha a grade em edição (live preview) */
function PxPreview({ grid, size }) {
  const ref = usePxRef(null);
  usePxEffect(() => {
    const cv = ref.current; if (!cv) return;
    cv.width = 16; cv.height = 16;
    const ctx = cv.getContext('2d');
    ctx.clearRect(0, 0, 16, 16);
    grid.forEach((c, i) => { if (c) { ctx.fillStyle = c; ctx.fillRect(i % 16, (i / 16) | 0, 1, 1); } });
  }, [grid]);
  return <canvas ref={ref} style={{ width: size, height: size, imageRendering: 'pixelated' }} />;
}

function PixelEditorModal({ sprite, title, onClose, onSaved }) {
  const [grid, setGrid] = usePxState(() => pxToGrid(sprite));
  const [tool, setTool] = usePxState('paint');
  const [color, setColor] = usePxState('#e23b5b');
  const undoRef = usePxRef([]);
  const paintingRef = usePxRef(false);
  const hasOverride = !!window.MX_TEX_OVERRIDES[sprite];

  // cores já usadas nesta textura (pra pegar fácil)
  const usedColors = [...new Set(pxToGrid(sprite).filter(Boolean))];

  function snapshot() { undoRef.current = [...undoRef.current.slice(-19), grid]; }
  function undo() {
    const st = undoRef.current;
    if (!st.length) return;
    setGrid(st[st.length - 1]);
    undoRef.current = st.slice(0, -1);
  }

  function applyAt(i, first) {
    if (tool === 'pick') {
      if (grid[i]) { setColor(grid[i]); setTool('paint'); }
      return;
    }
    if (first) snapshot();
    if (tool === 'fill') { setGrid(g => pxFlood(g, i, color)); return; }
    const v = tool === 'erase' ? null : color;
    setGrid(g => (g[i] === v ? g : (() => { const n = g.slice(); n[i] = v; return n; })()));
  }

  usePxEffect(() => {
    const up = () => { paintingRef.current = false; };
    window.addEventListener('pointerup', up);
    return () => window.removeEventListener('pointerup', up);
  }, []);

  function save() {
    window.MX_TEX_OVERRIDES[sprite] = pxFromGrid(grid);
    pxPersist();
    window.dispatchEvent(new CustomEvent('mx-tex', { detail: { sprite } }));
    onSaved(sprite);
    onClose();
  }
  function restore() {
    delete window.MX_TEX_OVERRIDES[sprite];
    pxPersist();
    window.dispatchEvent(new CustomEvent('mx-tex', { detail: { sprite } }));
    setGrid(pxToGrid(sprite));
    undoRef.current = [];
  }

  return (
    <div className="mx-veil" onClick={e => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="mx-px" data-screen-label="Pixel editor de textura">
        <button className="mx-exp-close" onClick={onClose} title="Fechar sem salvar">✕</button>
        <h2>Editor de textura</h2>
        <div className="mx-exp-sub">{title || sprite} · 16×16 · usada em todos os registros com esta textura</div>

        <div className="mx-px-body">
          {/* ferramentas */}
          <div className="mx-px-tools">
            {PX_TOOLS.map(tl => (
              <button key={tl.id} type="button" className={`mx-px-tool${tool === tl.id ? ' on' : ''}`}
                onClick={() => setTool(tl.id)} title={tl.label}>
                <window.Icon name={tl.ico} style="line" size={15} />
                <span>{tl.label}</span>
              </button>
            ))}
            <div className="mx-px-sep" />
            <button type="button" className="mx-px-tool" onClick={undo} title="Desfazer (até 20 passos)">
              <window.Icon name="history" style="line" size={15} /><span>Desfazer</span>
            </button>
            {hasOverride && (
              <button type="button" className="mx-px-tool danger" onClick={restore} title="Voltar pra textura original">
                <window.Icon name="trash" style="line" size={15} /><span>Original</span>
              </button>
            )}
          </div>

          {/* tela de pintura */}
          <div className="mx-px-board" onPointerDown={() => { paintingRef.current = true; }}>
            {grid.map((c, i) => (
              <div key={i} className="mx-px-cell" style={c ? { background: c } : null}
                onPointerDown={e => { e.preventDefault(); applyAt(i, true); }}
                onPointerEnter={() => { if (paintingRef.current && (tool === 'paint' || tool === 'erase')) applyAt(i, false); }} />
            ))}
          </div>

          {/* paleta + preview */}
          <div className="mx-px-side">
            <div className="mx-px-label">Cores da textura</div>
            <div className="mx-px-swatches">
              {usedColors.map(c => (
                <button key={'u' + c} type="button" className={`mx-px-sw${color === c && tool !== 'erase' ? ' on' : ''}`}
                  style={{ background: c }} onClick={() => { setColor(c); if (tool === 'erase' || tool === 'pick') setTool('paint'); }} title={c} />
              ))}
            </div>
            <div className="mx-px-label">Paleta</div>
            <div className="mx-px-swatches">
              {PX_COLORS.map(c => (
                <button key={c} type="button" className={`mx-px-sw${color === c && tool !== 'erase' ? ' on' : ''}`}
                  style={{ background: c }} onClick={() => { setColor(c); if (tool === 'erase' || tool === 'pick') setTool('paint'); }} title={c} />
              ))}
            </div>
            <div className="mx-px-label">Preview</div>
            <div className="mx-px-previews">
              <span className="mx-px-slot"><PxPreview grid={grid} size={48} /></span>
              <span className="mx-px-slot"><PxPreview grid={grid} size={28} /></span>
              <span className="mx-px-slot"><PxPreview grid={grid} size={16} /></span>
            </div>
          </div>
        </div>

        <div className="mx-px-foot">
          <button className="mx-save" onClick={save}>Salvar textura</button>
          <button className="mx-cancel" onClick={onClose}>Cancelar</button>
          <span className="mx-px-hint">Arraste pra pintar · o balde preenche áreas contínuas</span>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { PixelEditorModal });
