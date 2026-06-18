/* ─────────────────────────────────────────────────────────────────────────
   Modrix — App shell: header, body (sidebar | editor | preview), status bar.
   Estado central + Tweaks (direções visuais).
   ───────────────────────────────────────────────────────────────────────── */
const { useState, useEffect, useRef, useCallback } = React;

const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "theme": "pro",
  "iconStyle": "line",
  "accent": "#2dd4bf",
  "fontUi": "Hanken Grotesk",
  "density": "regular",
  "showPreview": true,
  "gameFeel": true
}/*EDITMODE-END*/;

const THEME_OPTS = [
  { id: 'pro', label: 'Refino Pro' },
  { id: 'workshop', label: 'Workshop' },
  { id: 'forge', label: 'Forge' },
  { id: 'beginner', label: 'Iniciante' },
];
// acento sugerido por tema (aplicado ao trocar de tema)
const THEME_ACCENT = { pro: '#2dd4bf', workshop: '#e89b46', forge: '#4cc2ff', beginner: '#12a594' };

function deepClone(o) { return JSON.parse(JSON.stringify(o)); }

// avança o patch de um semver (1.4.0 → 1.4.1); se não casar, devolve igual
function bumpPatch(v) {
  const m = String(v || '').match(/^(\d+)\.(\d+)\.(\d+)(.*)$/);
  if (!m) return v;
  return `${m[1]}.${m[2]}.${Number(m[3]) + 1}${m[4] || ''}`;
}

function uniqueId(base, arr) {
  const ids = new Set((arr || []).map(r => r.id));
  if (!ids.has(base)) return base;
  let n = 2; while (ids.has(`${base}_${n}`)) n++; return `${base}_${n}`;
}
function makeNew(key, m) {
  const arr = m[key] || [];
  if (key === 'items') return { kind: 'item', id: uniqueId('novo_item', arr), name: 'Novo Item', sprite: 'apple', stackSize: 64, durability: 0, rarity: 'COMMON', fireResistant: false, attackDamage: 0, attackSpeed: 0, consumable: 'none', enchantable: false };
  if (key === 'blocks') return { kind: 'block', id: uniqueId('novo_bloco', arr), name: 'Novo Bloco', sprite: 'ore', hardness: 1.5, resistance: 1.5, requiresTool: true, tool: 'pickaxe', toolTier: 'wood', lightLevel: 0, drops: '', xpMin: 0, xpMax: 0 };
  if (key === 'recipes') return { kind: 'recipe', id: uniqueId('nova_receita', arr), name: 'Nova Receita', sprite: 'sword', type: 'shaped', grid: Array(9).fill(null), result: 'espada_de_rubi', count: 1, cookTime: 200, xp: 0.7 };
  if (key === 'tools') return { kind: 'tool', id: uniqueId('nova_ferramenta', arr), name: 'Nova Ferramenta', sprite: 'picareta', toolType: 'pickaxe', tier: 'iron', durability: 250, speed: 6, attackDamage: 1, attackSpeed: -2.8, rarity: 'COMMON', stackSize: 1, consumable: 'none', enchantable: true, fireResistant: false };
  if (key === 'armor') return { kind: 'armor', id: uniqueId('nova_armadura', arr), name: 'Nova Armadura', sprite: 'peitoral', pieces: ['helmet', 'chestplate', 'leggings', 'boots'], durabilityMult: 15, toughness: 0, knockbackRes: 0, enchantability: 9, rarity: 'COMMON' };
  if (key === 'ores') return { kind: 'ore', id: uniqueId('novo_veio', arr), name: 'Novo Veio', sprite: 'ore', block: (m.blocks && m.blocks[0] ? m.blocks[0].id : ''), veinSize: 8, perChunk: 6, minY: -64, maxY: 64, dimension: 'overworld', deepslate: true };
  if (key === 'effects') return { kind: 'effect', id: uniqueId('novo_efeito', arr), name: 'Novo Efeito', sprite: 'pocao', category: 'beneficial', behavior: 'regen', color: '#5ce0a3', amplifier: 0, duration: 30, particles: true, icon: true };
  if (key === 'mobs') return { kind: 'mob', id: uniqueId('novo_mob', arr), name: 'Novo Mob', sprite: 'golem', category: 'hostile', health: 20, damage: 3, speed: 0.23, knockbackRes: 0, armor: 0, xpDrop: 5, dropItem: 'rubi', dropMin: 0, dropMax: 2, spawns: true, spawnLight: 7 };
  if (key === 'trees') return { kind: 'tree', id: uniqueId('nova_arvore', arr), name: 'Nova Árvore', sprite: 'folhas', trunk: 'tronco', leaves: 'folhas', minHeight: 4, maxHeight: 7, shape: 'blob', perChunk: 2, dimension: 'overworld' };
  return null;
}
// Validação universal de qualquer registro (delega receita pro validateRecipe).
function validateDraft(draft, mod, sel) {
  if (!draft) return [];
  if (draft.kind === 'recipe') return window.validateRecipe ? window.validateRecipe(draft) : [];
  const issues = [];
  if (!String(draft.name || '').trim()) issues.push({ level: 'error', msg: 'Dê um nome de exibição.' });
  if (!String(draft.id || '').trim()) issues.push({ level: 'error', msg: 'Defina um ID de registro.' });
  else {
    const dup = (mod[sel.key] || []).some(r => r.id === draft.id && r.id !== sel.id);
    if (dup) issues.push({ level: 'error', msg: `Já existe um registro com o ID “${draft.id}” nesta seção.` });
  }
  if (draft.kind === 'item' && draft.consumable === 'food' && !(draft.nutrition > 0))
    issues.push({ level: 'warn', msg: 'Comida sem nutrição não restaura fome.' });
  if (draft.kind === 'block' && draft.requiresTool && !draft.drops)
    issues.push({ level: 'warn', msg: 'Bloco exige ferramenta mas não tem drop — não soltará nada ao quebrar.' });
  if (draft.kind === 'tool' && !(draft.durability > 0))
    issues.push({ level: 'error', msg: 'Durabilidade deve ser maior que zero.' });
  if (draft.kind === 'armor' && (draft.pieces || []).length === 0)
    issues.push({ level: 'error', msg: 'Selecione ao menos uma peça do conjunto.' });
  if (draft.kind === 'mob' && !(draft.health > 0))
    issues.push({ level: 'error', msg: 'A vida (HP) deve ser maior que zero.' });
  if (draft.kind === 'ore' && !draft.block)
    issues.push({ level: 'error', msg: 'Escolha o bloco gerado.' });
  return issues;
}

function firstAvailable(m, exceptKey, exceptId) {
  for (const k of ['items', 'blocks', 'tools', 'armor', 'recipes', 'effects', 'mobs', 'ores', 'trees']) {
    const arr = (m[k] || []).filter(r => !(k === exceptKey && r.id === exceptId));
    if (arr.length) return { key: k, id: arr[0].id };
  }
  return null;
}

// ── Empty states por seção: cada oficina vazia conta a sua própria história ──
const MX_EMPTY = {
  items:   { sprite: 'apple',    title: 'A bancada está limpa',            cta: 'Criar primeiro item',
    blurb: 'Todo grande mod começa com um item na mão — uma gema, uma chave, um troféu raro. Forje o primeiro e veja-o ganhar vida no preview ao lado.' },
  blocks:  { sprite: 'ore',      title: 'Nenhum bloco assentado',          cta: 'Criar primeiro bloco', pack: true,
    blurb: 'Blocos são o esqueleto do seu mundo: minérios pra minerar, decoração, máquinas. Assente o primeiro tijolo da sua criação.' },
  tools:   { sprite: 'picareta', title: 'A bancada de ferramentas vazia',  cta: 'Criar ferramenta', pack: true,
    blurb: 'Picaretas, machados, espadas — dê ao jogador algo pra empunhar e sentir o peso do material que você inventou.' },
  armor:   { sprite: 'peitoral', title: 'O armário está vazio',            cta: 'Criar armadura', pack: true,
    blurb: 'Do capacete às botas: monte um conjunto que protege de verdade e ainda deixa o jogador com um visual e tanto.' },
  recipes: { sprite: 'sword',    title: 'Nenhuma receita no livro',        cta: 'Criar receita',
    blurb: 'Receitas conectam tudo: diga ao jogo como fabricar, fundir e forjar aquilo que você já criou. É aqui que o mod vira jogável.' },
  effects: { sprite: 'pocao',    title: 'Nada borbulhando no caldeirão',   cta: 'Criar efeito',
    blurb: 'Regeneração, veneno, brilho rúbeo — efeitos de status e poções que mudam o ritmo de cada combate.' },
  mobs:    { sprite: 'golem',    title: 'O bestiário está vazio',          cta: 'Criar mob', pack: true,
    blurb: 'Popule seu mundo com criaturas — pacíficas, neutras ou nem um pouco amigáveis. Cada uma com vida, drops e onde surge.' },
  ores:    { sprite: 'ore',      title: 'O subsolo está intocado',         cta: 'Criar geração', pack: true,
    blurb: 'Espalhe veios pelas camadas do mundo pra dar ao jogador o que procurar lá embaixo, picareta na mão.' },
  trees:   { sprite: 'folhas',   title: 'Nenhuma árvore plantada',         cta: 'Criar árvore',
    blurb: 'Faça florestas crescerem com troncos, copas e formatos só seus — do arbusto à árvore gigante.' },
};

function SectionEmptyState({ secKey, iconStyle, onCreate, onPack }) {
  const e = MX_EMPTY[secKey];
  if (!e) {
    return (
      <div className="mx-main">
        <div className="mx-empty-state">
          <div className="mx-empty-slot"><window.Icon name="spark" style={iconStyle} size={34} /></div>
          <h2>Esta seção está vazia</h2>
          <p>Crie o primeiro registro pra começar.</p>
        </div>
      </div>
    );
  }
  return (
    <div className="mx-main">
      <div className="mx-empty-state">
        <div className="mx-empty-slot art"><window.PixelSprite sprite={e.sprite} size={58} /></div>
        <h2>{e.title}</h2>
        <p>{e.blurb}</p>
        <div className="mx-empty-actions">
          <button className="mx-save" onClick={() => onCreate(secKey)}>+ {e.cta}</button>
          {e.pack && <button className="mx-empty-alt" onClick={onPack}>ou começar com um conjunto pronto</button>}
        </div>
      </div>
    </div>
  );
}

// ── Projeto pendente vindo do launcher (abrir/forjar) ──
function readPending() {
  try { return JSON.parse(localStorage.getItem('modrix_open_project_v1') || 'null'); } catch (e) { return null; }
}
const MX_PENDING = readPending();
const MX_PROJ_ID = MX_PENDING ? (MX_PENDING.type === 'new' ? (MX_PENDING.modId || 'meu_mod') : MX_PENDING.id) : 'aldoria';
function computeInitialMod() {
  // conteúdo já persistido deste projeto (qualquer tipo)
  const stored = window.mxLoadMod ? window.mxLoadMod(MX_PROJ_ID) : null;
  if (stored) {
    // migração: arrays adicionados depois do save (tools/armor/ores…) entram
    // com o conteúdo-semente (projeto demo) ou vazios (projetos do usuário)
    for (const k of ['items', 'blocks', 'tools', 'armor', 'recipes', 'effects', 'mobs', 'ores', 'trees']) {
      if (!Array.isArray(stored[k])) stored[k] = MX_PROJ_ID === 'aldoria' ? deepClone(window.MX_MOD[k] || []) : [];
    }
    if (!Array.isArray(stored.changelog)) stored.changelog = MX_PROJ_ID === 'aldoria' ? deepClone(window.MX_MOD.changelog || []) : [];
    if (typeof stored.releaseNotes !== 'string') stored.releaseNotes = '';
    return stored;
  }
  if (MX_PENDING && MX_PENDING.type === 'new' && window.mxBuildKitMod) return window.mxBuildKitMod(MX_PENDING);
  const m = deepClone(window.MX_MOD);
  if (MX_PENDING && MX_PENDING.type === 'existing' && MX_PENDING.id && MX_PENDING.id !== 'aldoria') {
    m.modName = MX_PENDING.name || m.modName;
    m.modId = MX_PENDING.id;
    m.modVersion = MX_PENDING.version || m.modVersion;
    m.folder = `~/mods/${MX_PENDING.id.replace(/_/g, '-')}`;
  }
  return m;
}
const MX_INITIAL_MOD = computeInitialMod();

function App() {
  const [t, setTweak] = useTweaks(TWEAK_DEFAULTS);

  // ── Estado do mod (do launcher, kit forjado, ou exemplo) ──
  const [mod, setMod] = useState(() => MX_INITIAL_MOD);
  const [sel, setSel] = useState({ key: 'overview', id: null });
  const [draft, setDraft] = useState(null);     // cópia em edição do registro selecionado
  const [dirty, setDirty] = useState(false);
  const [popKey, setPopKey] = useState(0);       // dispara animação de "pop" no preview
  const [toast, setToast] = useState(null);
  const [pvTab, setPvTab] = useState(null);
  const [view, setView] = useState('editor');   // 'editor' | 'map'
  const [fxKey, setFxKey] = useState(0);         // dispara partículas no preview
  const [tourStep, setTourStep] = useState(null); // null = tour desligado
  const [exporting, setExporting] = useState(false);
  const [javaOpen, setJavaOpen] = useState(false);
  const [testing, setTesting] = useState(false);
  const [texEdit, setTexEdit] = useState(null); // sprite sendo pintado
  const [qsOpen, setQsOpen] = useState(false);  // quick switcher (Ctrl+K)
  const [packOpen, setPackOpen] = useState(false); // galeria de conjuntos prontos

  const dupRef = useRef(null);
  // Ctrl+K abre o quick switcher · Ctrl+D duplica o registro atual
  useEffect(() => {
    function onKey(e) {
      if ((e.ctrlKey || e.metaKey) && (e.key === 'k' || e.key === 'K')) { e.preventDefault(); setQsOpen(o => !o); }
      else if ((e.ctrlKey || e.metaKey) && (e.key === 'd' || e.key === 'D')) { e.preventDefault(); dupRef.current && dupRef.current(); }
    }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);
  const [consoleOpen, setConsoleOpen] = useState(false);
  const [clog, setClog] = useState(() => [
    `[${new Date().toLocaleTimeString('pt-BR')}] modrix iniciado · NeoForge ${MX_INITIAL_MOD.neoforge} · Java 21`,
    `[${new Date().toLocaleTimeString('pt-BR')}] projeto “${MX_INITIAL_MOD.modName}” carregado (${(MX_INITIAL_MOD.items || []).length} itens · ${(MX_INITIAL_MOD.blocks || []).length} blocos · ${(MX_INITIAL_MOD.recipes || []).length} receitas)`,
    `[${new Date().toLocaleTimeString('pt-BR')}] gradle daemon pronto · aguardando alterações`,
  ]);
  const clogRef = useRef(null);
  function pushLog(line) {
    setClog(l => [...l.slice(-120), `[${new Date().toLocaleTimeString('pt-BR')}] ${line}`]);
  }
  useEffect(() => {
    if (clogRef.current) clogRef.current.scrollTop = clogRef.current.scrollHeight;
  }, [clog, consoleOpen]);

  // ── XP do ferreiro (compartilhado com o launcher via localStorage) ──
  const [xpInfo, setXpInfo] = useState(() => (window.mxComputeXp ? window.mxComputeXp() : null));
  const [xpGain, setXpGain] = useState(null);
  function gainXp(n) {
    if (!window.mxComputeXp) return;
    const before = window.mxComputeXp().level;
    try {
      const v = Number(localStorage.getItem('modrix_xp_v1') || 0) + n;
      localStorage.setItem('modrix_xp_v1', String(v));
    } catch (e) {}
    const after = window.mxComputeXp();
    setXpInfo(after);
    setXpGain({ amount: n, key: Date.now() });
    if (after.level > before && t.gameFeel) {
      setTimeout(() => {
        setFxKey(k => k + 1);
        showToast({ msg: `Subiu de nível! — Nível ${after.level}`, sub: `rank: ${after.rank.title}`, sprite: 'rubi', ach: true });
      }, 1500);
    }
  }

  // ── Persistência: grava conteúdo do mod + meta do projeto no storage ──
  function persistMod(m) {
    if (!window.mxSaveMod) return;
    window.mxSaveMod(MX_PROJ_ID, m);
    if (window.mxTouchProject) {
      const patch = {
        name: m.modName, version: m.modVersion,
        items: (m.items || []).length + (m.tools || []).length, blocks: (m.blocks || []).length, recipes: (m.recipes || []).length,
        edited: 'agora mesmo',
      };
      if (m.emblem) patch.emblem = m.emblem;
      window.mxTouchProject(MX_PROJ_ID, patch);
    }
  }

  // ── Conquistas ao vivo ──
  function checkAchievements(m, savedDraft) {
    if (!window.mxUnlockAch) return;
    const cand = [];
    if ((m.items || []).length >= 1) cand.push('first_item');
    if ((m.blocks || []).length >= 1) cand.push('first_block');
    if ((m.armor || []).length >= 1) cand.push('armory');
    if ((m.mobs || []).length >= 1) cand.push('bestiary');
    if ((m.ores || []).length >= 1 || (m.trees || []).length >= 1) cand.push('naturalist');
    if ((m.blocks || []).length >= 10) cand.push('ore_baron');
    if ((m.recipes || []).length >= 10) cand.push('ten_recipes');
    if (savedDraft && savedDraft.kind === 'recipe' && savedDraft.type === 'smithing') cand.push('smith');
    for (const id of cand) {
      const ach = window.mxUnlockAch(id);
      if (ach) {
        setTimeout(() => {
          setFxKey(k => k + 1);
          setXpInfo(window.mxComputeXp());
          setXpGain({ amount: 40, key: Date.now() });
          showToast({ msg: `Conquista: ${ach.name}!`, sub: `${ach.desc} · +40 XP`, sprite: ach.emblem, ach: true });
        }, 1700);
        return; // uma por vez — a próxima sai no próximo save
      }
    }
  }

  function launchDone() {
    pushLog(`runClient OK · mundo "modrix_test" carregado · 0 erros`);
    logAct('Testado no jogo — 0 erros', 'tabua');
    const ach = window.mxUnlockAch && window.mxUnlockAch('tested');
    setXpInfo(window.mxComputeXp ? window.mxComputeXp() : null);
    if (ach) {
      setFxKey(k => k + 1);
      setXpGain({ amount: 40, key: Date.now() });
      showToast({ msg: `Conquista: ${ach.name}!`, sub: `${ach.desc} · +40 XP`, sprite: ach.emblem, ach: true });
    }
  }

  function texSaved(spriteName) {
    pushLog(`✎ textura "${spriteName}" repintada · assets/textures atualizados`);
    logAct('Textura repintada', spriteName);
    setFxKey(k => k + 1);
    showToast(t.gameFeel
      ? { msg: 'Textura salva!', sub: 'aplicada em todos os registros · +15 XP', sprite: spriteName, ach: true }
      : { msg: 'Textura salva · assets atualizados', ic: 'check' });
    gainXp(15);
    const ach = window.mxUnlockAch && window.mxUnlockAch('artist');
    if (ach) {
      setTimeout(() => {
        setXpInfo(window.mxComputeXp());
        setXpGain({ amount: 40, key: Date.now() });
        showToast({ msg: `Conquista: ${ach.name}!`, sub: `${ach.desc} · +40 XP`, sprite: ach.emblem, ach: true });
      }, 1700);
    }
  }

  function publishDone() {
    pushLog(`BUILD SUCCESSFUL · ${mod.modId}-${mod.modVersion}.jar publicado`);
    logAct(`v${mod.modVersion} publicada (.jar)`, 'lingote');
    // carimba a versão no changelog e avança o patch
    setMod(m => {
      const meses = ['jan', 'fev', 'mar', 'abr', 'mai', 'jun', 'jul', 'ago', 'set', 'out', 'nov', 'dez'];
      const d = new Date();
      const entry = {
        version: m.modVersion,
        date: `${d.getDate()} ${meses[d.getMonth()]} ${d.getFullYear()}`,
        notes: (m.releaseNotes || '').trim() || 'Publicação sem notas.',
        items: (m.items || []).length + (m.tools || []).length, blocks: (m.blocks || []).length, recipes: (m.recipes || []).length,
      };
      const log = (m.changelog || []).slice();
      // se a versão atual já está no topo (não publicada), substitui em vez de duplicar
      if (log[0] && log[0].version === m.modVersion) log[0] = entry;
      else log.unshift(entry);
      const bumped = bumpPatch(m.modVersion);
      // limpa o marcador "novidade" — tudo publicado deixa de ser novo
      const c = { ...m, changelog: log.slice(0, 12), releaseNotes: '', modVersion: bumped };
      for (const k of ['items', 'blocks', 'tools', 'armor', 'recipes', 'effects', 'mobs', 'ores', 'trees']) {
        if (Array.isArray(c[k])) c[k] = c[k].map(r => { if (r._new) { const { _new, ...rest } = r; return rest; } return r; });
      }
      persistMod(c);
      return c;
    });
    const ach = window.mxUnlockAch && window.mxUnlockAch('published');
    setXpInfo(window.mxComputeXp ? window.mxComputeXp() : null);
    if (ach) {
      setFxKey(k => k + 1);
      setXpGain({ amount: 40, key: Date.now() });
      showToast({ msg: `Conquista: ${ach.name}`, sub: `${ach.desc} · +40 XP`, sprite: ach.emblem, ach: true });
    } else {
      showToast({ msg: 'Versão publicada e registrada no histórico', ic: 'check' });
    }
  }

  // acento do projeto recém-aberto (cor do mod escolhida no launcher)
  useEffect(() => {
    if (MX_PENDING && MX_PENDING.accent && !MX_PENDING.accentApplied) {
      setTweak('accent', MX_PENDING.accent);
      try { localStorage.setItem('modrix_open_project_v1', JSON.stringify({ ...MX_PENDING, accentApplied: true })); } catch (e) {}
    }
  }, []);

  // tour de boas-vindas: auto-inicia na primeira visita
  useEffect(() => {
    try {
      if (!localStorage.getItem('modrix_tour_done_v1')) setTourStep(0);
    } catch (e) { /* storage indisponível */ }
  }, []);

  function tourNext() {
    if (tourStep >= window.TOUR_STEPS.length - 1) {
      endTour();
      if (t.gameFeel) {
        setFxKey(k => k + 1);
        showToast({ msg: 'Conquista: Primeiro passo!', sub: 'tour de boas-vindas concluído', sprite: 'sword', ach: true });
      }
    } else setTourStep(s => s + 1);
  }
  function endTour() {
    setTourStep(null);
    try { localStorage.setItem('modrix_tour_done_v1', '1'); } catch (e) {}
  }
  function restartTour() {
    setView('editor');
    const first = firstAvailable(mod);
    if (first) setSel(first);
    setTourStep(0);
  }

  // registro atualmente selecionado (do mod salvo)
  const current = (mod[sel.key] || []).find(r => r.id === sel.id);

  // ao trocar de seleção, recarrega o draft
  useEffect(() => {
    if (current) { setDraft(deepClone(current)); setDirty(false); }
  }, [sel.key, sel.id]);

  // aplica tema → também ajusta accent var
  useEffect(() => {
    const root = document.querySelector('.mx-window');
    if (root) { root.setAttribute('data-theme', t.theme); root.setAttribute('data-density', t.density); }
    document.documentElement.style.setProperty('--accent-live', t.accent);
  }, [t.theme, t.density, t.accent]);

  // override do acento (sobrescreve o do tema)
  useEffect(() => {
    const root = document.querySelector('.mx-window');
    if (root) root.style.setProperty('--accent', t.accent);
  }, [t.accent]);

  // fonte de UI
  useEffect(() => {
    document.documentElement.style.setProperty('--font-ui', `'${t.fontUi}', system-ui, sans-serif`);
  }, [t.fontUi]);

  const set = useCallback((patch) => {
    setDraft(d => ({ ...d, ...patch }));
    setDirty(true);
    setPopKey(k => k + 1);
  }, []);

  // metadados do projeto (Visão geral): aplica e persiste na hora
  function setModMeta(patch) {
    setMod(m => { const c = { ...m, ...patch }; persistMod(c); return c; });
  }

  // atividade recente (persistida por projeto)
  function logAct(msg, sprite) {
    if (window.mxPushActivity) window.mxPushActivity(MX_PROJ_ID, { msg, sprite: sprite || null, ts: Date.now() });
  }

  function selectItem(key, id) { setSel({ key, id }); }
  function openFromMap(key, id) { setSel({ key, id }); setView('editor'); }

  function addStub(sec) {
    if (!sec.dynamic) { showToast({ msg: `“${sec.name}” ainda é demo neste protótipo`, ic: 'spark' }); return; }
    const rec = makeNew(sec.dynamic, mod);
    if (!rec) return;
    rec._new = true;
    const c = deepClone(mod);
    (c[sec.dynamic] = c[sec.dynamic] || []).push(deepClone(rec));
    setMod(c); persistMod(c);
    setSel({ key: sec.dynamic, id: rec.id });
    showToast(t.gameFeel
      ? { msg: `${rec.name} criado`, sub: 'pronto pra editar · +20 XP', sprite: rec.sprite }
      : { msg: `${rec.name} criado · edite os campos`, ic: 'spark' });
    gainXp(20);
    checkAchievements(c, null);
    logAct(`${rec.name} criado`, rec.sprite);
    pushLog(`+ ${rec.id} criado em ${sec.dynamic} · registry atualizado`);
  }
  function addPack(pack, material) {
    // garante material único: se algum id colidir, sufixa o nome do material
    const baseId = window.toId(material) || 'material';
    let mat = material, id = baseId, tries = 1;
    const collides = (idb) => pack.build(material, idb).some(({ key, rec }) => (mod[key] || []).some(r => r.id === rec.id));
    while (collides(id) && tries < 20) { tries++; mat = `${material} ${tries}`; id = `${baseId}_${tries}`; }
    const recs = pack.build(mat, id);
    const c = deepClone(mod);
    recs.forEach(({ key, rec }) => { (c[key] = c[key] || []).push({ ...deepClone(rec), _new: true }); });
    setMod(c); persistMod(c);
    const first = recs[0];
    setSel({ key: first.key, id: first.rec.id });
    setPackOpen(false);
    showToast(t.gameFeel
      ? { msg: `${pack.name} adicionado`, sub: `${recs.length} registros · +${recs.length * 20} XP`, sprite: pack.sprites[0] }
      : { msg: `${pack.name}: ${recs.length} registros criados`, ic: 'spark' });
    gainXp(recs.length * 20);
    checkAchievements(c, null);
    logAct(`Conjunto “${pack.name}” adicionado (${recs.length} registros)`, pack.sprites[0]);
    pushLog(`+ pack ${pack.id} → ${recs.map(r => r.rec.id).join(', ')}`);
  }
  function del() {
    const key = sel.key;
    const removed = draft ? deepClone(draft) : null;
    const idx = (mod[key] || []).findIndex(r => r.id === sel.id);
    const name = draft ? draft.name : '';
    const next = firstAvailable(mod, key, sel.id);
    const c = deepClone(mod);
    c[key] = (c[key] || []).filter(r => r.id !== sel.id);
    setMod(c); persistMod(c);
    setSel(next || { key: 'overview', id: null });
    showToast({
      msg: `${name || 'registro'} excluído`, ic: 'check',
      action: removed ? { label: 'Desfazer', fn: () => undoDelete(key, removed, idx) } : null,
    }, 6000);
    logAct(`${name || 'registro'} excluído`, null);
    pushLog(`- ${sel.id} removido · código regenerado`);
  }
  function undoDelete(key, rec, idx) {
    setMod(prev => {
      const c = deepClone(prev);
      const arr = c[key] || (c[key] = []);
      if (arr.some(r => r.id === rec.id)) return prev; // já restaurado
      arr.splice(Math.min(idx, arr.length), 0, rec);
      persistMod(c);
      return c;
    });
    setSel({ key, id: rec.id });
    showToast({ msg: `${rec.name} restaurado`, ic: 'check', sprite: rec.sprite });
  }
  function dup() {
    if (!draft) return;
    const key = sel.key;
    const copy = deepClone(draft);
    copy.id = uniqueId(draft.id || 'copia', mod[key] || []);
    copy.name = `${draft.name} (cópia)`;
    copy._idEdited = true;
    copy._new = true;
    const idx = (mod[key] || []).findIndex(r => r.id === sel.id);
    const c = deepClone(mod);
    const arr = c[key] || (c[key] = []);
    arr.splice(idx + 1, 0, copy);
    setMod(c); persistMod(c);
    setSel({ key, id: copy.id });
    showToast(t.gameFeel
      ? { msg: `${copy.name} criado`, sub: 'cópia pronta pra ajustar · +20 XP', sprite: copy.sprite }
      : { msg: `${copy.name} criado`, ic: 'spark' });
    gainXp(20);
    logAct(`${copy.name} criado`, copy.sprite);
    pushLog(`+ ${copy.id} duplicado de ${sel.id}`);
  }
  dupRef.current = dup;
  function mapStub(label) {
    showToast({ msg: `“${label}” ainda não tem editor neste protótipo`, ic: 'spark' });
  }

  function save() {
    if (validateDraft(draft, mod, sel).some(i => i.level === 'error')) return;
    const copy = deepClone(mod);
    const arr = copy[sel.key] || [];
    const i = arr.findIndex(r => r.id === current.id);
    if (i >= 0) arr[i] = deepClone(draft);
    copy[sel.key] = arr;
    setMod(copy); persistMod(copy);
    if (draft.id !== sel.id) setSel({ key: sel.key, id: draft.id }); // id mudou → re-seleciona
    setDirty(false);
    setPopKey(k => k + 1);
    setFxKey(k => k + 1);
    showToast(t.gameFeel
      ? { msg: `${draft.name} salvo!`, sub: 'código Java regenerado · +10 XP', sprite: draft.sprite, ach: true }
      : { msg: `${draft.name} salvo · código Java regenerado`, ic: 'check' });
    gainXp(10);
    checkAchievements(copy, draft);
    logAct(`${draft.name} salvo`, draft.sprite);
    pushLog(`✓ ${draft.id} salvo → ModItems.java / datagen regenerados · BUILD OK`);
  }
  function revert() { setDraft(deepClone(current)); setDirty(false); }

  const toastTimer = useRef(null);
  function showToast(tt, ms) {
    setToast(tt); clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(() => setToast(null), ms || 2600);
  }

  // ready = há registro selecionado e draft carregado; senão, oficina vazia
  const ready = !!current && !!draft;
  if (current && !draft) return null; // frame de carga do draft
  const isBlock = ready && draft.kind === 'block';
  const isRecipe = ready && draft.kind === 'recipe';
  const isTool = ready && draft.kind === 'tool';
  const isArmor = ready && draft.kind === 'armor';
  const isOre = ready && draft.kind === 'ore';
  const isEffect = ready && draft.kind === 'effect';
  const isMob = ready && draft.kind === 'mob';
  const isTree = ready && draft.kind === 'tree';
  const kindLabel = !ready ? '' : isRecipe ? 'Receita' : isBlock ? 'Bloco' : isTool ? 'Ferramenta' : isArmor ? 'Armadura' : isOre ? 'Minério' : isEffect ? 'Efeito' : isMob ? 'Mob' : isTree ? 'Árvore' : (draft.consumable === 'food' ? 'Item · Comida' : 'Item');
  const issues = ready ? validateDraft(draft, mod, sel) : [];
  const blocked = issues.some(i => i.level === 'error');
  const canSave = dirty && !blocked;

  return (
    <div className="mx-window" data-theme={t.theme} data-density={t.density}>
      {/* ── Header ─────────────────────────────────────────────── */}
      <header className="mx-header">
        <div className="mx-h-left">
          <button className="mx-back" title="Voltar aos projetos" onClick={() => { window.location.href = 'Modrix Início.html'; }}><window.Icon name="folder" style={t.iconStyle} size={17} /></button>
          <span className="mx-logo">
            <svg width="15" height="15" viewBox="0 0 15 15" aria-hidden="true">
              <rect x="5" y="0" width="5" height="5" fill="var(--accent)" opacity="0.5"></rect>
              <rect x="0" y="5" width="5" height="5" fill="var(--accent)"></rect>
              <rect x="10" y="5" width="5" height="5" fill="var(--accent)" opacity="0.75"></rect>
              <rect x="5" y="10" width="5" height="5" fill="var(--accent)" opacity="0.3"></rect>
            </svg>
            MODRIX
          </span>
          <span className="mx-divider" />
          <span className="mx-proj mx-proj-link" title="Visão geral do projeto" onClick={() => { setView('editor'); setSel({ key: 'overview', id: null }); }}>{mod.modName}</span>
          <span className="mx-chip">{mod.modId} · v{mod.modVersion}</span>
        </div>
        <nav className="mx-htabs">
          <div className="mx-seg">
            <button type="button" className={view === 'editor' ? 'on' : ''} onClick={() => setView('editor')}>Editor</button>
            <button type="button" className={view === 'map' ? 'on' : ''} onClick={() => setView('map')}>Mapa do mod</button>
            <button type="button" className={view === 'showcase' ? 'on' : ''} onClick={() => setView('showcase')}>Vitrine</button>
          </div>
        </nav>
        <div className="mx-h-right">
          {xpInfo && (
            <div className="mx-lvl" title={`${xpInfo.xp} XP · rank ${xpInfo.rank.title}`}>
              <span className="mx-lvl-n">Nv {xpInfo.level}</span>
              <span className="mx-lvl-bar"><i style={{ width: `${xpInfo.pct}%` }} /></span>
              {xpGain && <span key={xpGain.key} className="mx-xp-float">+{xpGain.amount} XP</span>}
            </div>
          )}
          <button className="mx-ghost-btn" onClick={() => setJavaOpen(true)}><window.Icon name="code" style={t.iconStyle} size={14} /> Ver Java</button>
          <button className="mx-ghost-btn" onClick={() => setExporting(true)}><window.Icon name="spark" style={t.iconStyle} size={14} /> Publicar</button>
          <button className="mx-run-btn" onClick={() => { setTesting(true); pushLog(`> gradle runClient — compilando ${mod.modId}…`); }}>
            <window.Icon name="run" style={t.iconStyle} size={13} /> Testar no jogo
          </button>
        </div>
      </header>

      {/* ── Corpo ──────────────────────────────────────────────── */}
      {view === 'map' ? (
        <div className="mx-body">
          <window.ModMap mod={mod} iconStyle={t.iconStyle} onOpen={openFromMap} onStub={mapStub} />
        </div>
      ) : view === 'showcase' ? (
        <div className="mx-body">
          <window.ShowcasePage mod={mod} iconStyle={t.iconStyle} />
        </div>
      ) : (
      <div className="mx-body">
        <window.Sidebar mod={mod} iconStyle={t.iconStyle}
          selectedKey={sel.key} selectedId={sel.id}
          onSelect={selectItem} onAdd={addStub} onPack={() => setPackOpen(true)} />

        <div className="mx-center">
          {sel.key === 'overview' ? (
            <window.OverviewDashboard mod={mod} setMeta={setModMeta}
              activity={window.mxActivity ? window.mxActivity(MX_PROJ_ID) : []}
              onEditTexture={s => setTexEdit({ sprite: s, title: 'Emblema do mod' })}
              onGo={(key, id) => setSel({ key, id })}
              onCreate={(key, label) => addStub({ key, dynamic: key, name: label })}
              onAction={a => { if (a === 'run') { setTesting(true); pushLog(`> gradle runClient — compilando ${mod.modId}…`); } else if (a === 'publish') setExporting(true); else if (a === 'java') setJavaOpen(true); else if (a === 'pack') setPackOpen(true); }} />
          ) : ready ? (<>
          <div className="mx-main">
            <div className="mx-breadcrumb">
              <span>{mod.modName}</span> <span>›</span>
              <span>{isRecipe ? 'Receitas' : isBlock ? 'Blocos' : isTool ? 'Ferramentas' : isArmor ? 'Armaduras' : isOre ? 'Minérios' : isEffect ? 'Efeitos' : isMob ? 'Mobs' : isTree ? 'Árvores' : 'Itens'}</span> <span>›</span>
              <b>{draft.name || 'sem nome'}</b>
            </div>
            <h1 className="mx-title">{draft.name || 'Sem nome'}</h1>
            <p className="mx-sub">{kindLabel} · editando <code style={{ fontFamily: 'var(--font-mono, monospace)', color: 'var(--text-muted)' }}>{mod.modId}:{draft.id}</code></p>

            {isRecipe
              ? <window.RecipeEditor rec={draft} modId={mod.modId} set={set} />
              : isArmor
                ? <window.ArmorEditor rec={draft} modId={mod.modId} set={set} onEditTexture={s => setTexEdit({ sprite: s, title: draft.name })} />
                : isOre
                  ? <window.OreEditor rec={draft} modId={mod.modId} set={set} />
                  : isEffect
                    ? <window.EffectEditor rec={draft} modId={mod.modId} set={set} />
                    : isMob
                      ? <window.MobEditor rec={draft} modId={mod.modId} set={set} onEditTexture={s => setTexEdit({ sprite: s, title: draft.name })} />
                      : isTree
                        ? <window.TreeEditor rec={draft} modId={mod.modId} set={set} onEditTexture={s => setTexEdit({ sprite: s, title: draft.name })} />
                        : isBlock
                          ? <window.BlockEditor rec={draft} modId={mod.modId} set={set} onEditTexture={s => setTexEdit({ sprite: s, title: draft.name })} />
                          : isTool
                            ? <window.ToolEditor rec={draft} modId={mod.modId} set={set} onEditTexture={s => setTexEdit({ sprite: s, title: draft.name })} />
                            : <window.ItemEditor rec={draft} modId={mod.modId} set={set} onEditTexture={s => setTexEdit({ sprite: s, title: draft.name })} />}
            <div style={{ height: 16 }} />
          </div>

          {/* ── Banner de validação universal (receita mostra o seu próprio) ── */}
          {!isRecipe && issues.length > 0 && (
            <div className="mx-valid" style={{ marginTop: 22 }}>
              {issues.map((it, i) => (
                <div key={i} className={`mx-valid-row ${it.level}`}>
                  <span className="mx-valid-dot" />{it.msg}
                </div>
              ))}
            </div>
          )}

          {/* ── Footer de ações ── */}
          <div className="mx-footer">
            <button className="mx-save" onClick={save} disabled={!canSave}>
              {dirty ? 'Salvar alterações' : 'Tudo salvo'}
            </button>
            <button className="mx-cancel" onClick={revert} disabled={!dirty}>Reverter</button>
            {dirty && !blocked && <span style={{ fontSize: 12, color: 'var(--yellow)', display: 'flex', alignItems: 'center', gap: 6 }}>
              <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--yellow)' }} /> alterações não salvas</span>}
            {blocked && <span style={{ fontSize: 12, color: 'var(--red)', display: 'flex', alignItems: 'center', gap: 6 }}>
              <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--red)' }} /> resolva os erros acima</span>}
            <button className="mx-dup" onClick={dup} title="Duplicar este registro"><window.Icon name="copy" style={t.iconStyle} size={13} fallback="grid" /> Duplicar</button>
            <button className="mx-del" onClick={del}>Excluir</button>
          </div>
          </>) : (
          <SectionEmptyState secKey={sel.key} iconStyle={t.iconStyle}
            onCreate={(k) => addStub({ key: k, dynamic: k, name: k })}
            onPack={() => setPackOpen(true)} />
          )}
        </div>

        {t.showPreview && ready && (isRecipe
          ? <window.RecipePreview rec={draft} modId={mod.modId} popKey={popKey} fxKey={fxKey} gameFeel={t.gameFeel} />
          : isArmor
            ? <window.ArmorPreview rec={draft} modId={mod.modId} popKey={popKey} />
            : isOre
              ? <window.OrePreview rec={draft} modId={mod.modId} popKey={popKey} />
              : isEffect
                ? <window.EffectPreview rec={draft} modId={mod.modId} popKey={popKey} />
                : isMob
                  ? <window.MobPreview rec={draft} modId={mod.modId} popKey={popKey} />
                  : isTree
                    ? <window.TreePreview rec={draft} modId={mod.modId} popKey={popKey} />
                    : <window.LivePreview rec={draft} modId={mod.modId} defaultTab={pvTab} popKey={popKey}
                      fxKey={fxKey} gameFeel={t.gameFeel} />)}
      </div>
      )}

      {/* ── Status bar ─────────────────────────────────────────── */}
      {/* ── Console (drawer acima da status bar) ── */}
      {consoleOpen && (
        <div className="mx-console" data-screen-label="Console">
          <div className="mx-console-head">
            <span><span className="mx-live-dot" /> Console · gradle</span>
            <button className="mx-console-x" onClick={() => setConsoleOpen(false)} title="Fechar">✕</button>
          </div>
          <pre className="mx-console-log" ref={clogRef}>{clog.join('\n')}</pre>
        </div>
      )}

      <footer className="mx-status">
        <div className="mx-st-group">
          <span className="mx-st-item" style={{ color: 'var(--green)' }}><span className="mx-live-dot" /> NeoForge {mod.neoforge}</span>
          <span className="mx-st-item">{(mod.items || []).length} itens · {(mod.blocks || []).length} blocos · {(mod.mobs || []).length} mobs · {(mod.recipes || []).length} receitas</span>
          <span className="mx-st-item" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono, monospace)' }}>{mod.folder}</span>
        </div>
        <div className="mx-st-group">
          <button className={`mx-st-btn${consoleOpen ? ' on' : ''}`} onClick={() => setConsoleOpen(o => !o)}><window.Icon name="console" style={t.iconStyle} size={13} /> Console</button>
          <span className="mx-st-item" style={{ color: 'var(--text-dim)' }}>Java 21 · Gradle pronto</span>
        </div>
      </footer>

      {/* ── Toast ──────────────────────────────────────────────── */}
      <div className={`mx-toast${toast ? ' show' : ''}${toast && toast.ach ? ' ach' : ''}`}>
        {toast && <>
          {toast.sprite
            ? <window.PixelSprite sprite={toast.sprite} size={28} />
            : <span className="ic"><window.Icon name={toast.ic || 'check'} style={t.iconStyle} size={15} /></span>}
          <span>
            <div className={toast.ach ? 'mx-ach-title' : ''}>{toast.msg}</div>
            {toast.sub && <div className="mx-ach-sub">{toast.sub}</div>}
          </span>
          {toast.action && <button className="mx-toast-action" onClick={() => { toast.action.fn(); setToast(null); }}>{toast.action.label}</button>}
        </>}
      </div>

      {/* ── Tweaks ─────────────────────────────────────────────── */}
      {/* ── Publicar (.jar) ── */}
      {exporting && <window.ExportModal mod={mod} onClose={() => setExporting(false)} onPublished={publishDone} />}
      {javaOpen && <window.JavaModal mod={mod} onClose={() => setJavaOpen(false)} />}
      {testing && <window.TestGameOverlay mod={mod} onClose={() => setTesting(false)} onLaunched={launchDone} />}
      {texEdit && <window.PixelEditorModal sprite={texEdit.sprite} title={texEdit.title} onClose={() => setTexEdit(null)} onSaved={texSaved} />}
      {qsOpen && <window.QuickSwitcher mod={mod}
        onPick={(key, id) => setSel({ key, id })}
        onAction={a => { if (a === 'run') setTesting(true); else if (a === 'publish') setExporting(true); else if (a === 'java') setJavaOpen(true); else if (a === 'pack') setPackOpen(true); }}
        onClose={() => setQsOpen(false)} />}
      {packOpen && <window.TemplateGallery onClose={() => setPackOpen(false)} onAdd={addPack} />}

      {/* tour de boas-vindas */}
      {tourStep !== null && (
        <window.OnboardingTour step={tourStep} onNext={tourNext} onSkip={endTour} />
      )}

      <TweaksPanel>
        <TweakSection label="Identidade — Refino Pro" />
        <TweakColor label="Cor de acento" value={t.accent}
          options={['#2dd4bf', '#4cc2ff', '#a78bfa', '#5ce0a3']}
          onChange={v => setTweak('accent', v)} />

        <TweakSection label="Interface" />
        <TweakRadio label="Ícones" value={t.iconStyle}
          options={[{ value: 'line', label: 'Linha' }, { value: 'emoji', label: 'Emoji' }]}
          onChange={v => setTweak('iconStyle', v)} />
        <TweakRadio label="Densidade" value={t.density}
          options={[{ value: 'compact', label: 'Densa' }, { value: 'regular', label: 'Padrão' }, { value: 'comfy', label: 'Folgada' }]}
          onChange={v => setTweak('density', v)} />
        <TweakSelect label="Fonte da UI" value={t.fontUi}
          options={['Hanken Grotesk', 'Inter Tight', 'Space Grotesk', 'IBM Plex Sans']}
          onChange={v => setTweak('fontUi', v)} />

        <TweakSection label="Layout" />
        <TweakToggle label="Preview ao vivo" value={t.showPreview}
          onChange={v => setTweak('showPreview', v)} />
        <TweakToggle label="Game feel (partículas, conquista)" value={t.gameFeel}
          onChange={v => setTweak('gameFeel', v)} />

        <TweakSection label="Onboarding" />
        <TweakButton label="Refazer tour de boas-vindas" onClick={restartTour} />
      </TweaksPanel>
    </div>
  );
}

ReactDOM.createRoot(document.getElementById('root')).render(<App />);
