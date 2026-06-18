/* ─────────────────────────────────────────────────────────────────────────
   Modrix — dados da tela inicial (launcher)
   Perfil do ferreiro, projetos (saves), templates, conquistas e ranks.
   ───────────────────────────────────────────────────────────────────────── */

const MX_PROFILE = {
  name: 'Adrian',
  handle: '@_y_levi',
  emblem: 'sword',
  streak: 5,
};

// projetos = "saves" do jogador. editedRank 0 = mais recente.
const MX_PROJECTS = [
  { id: 'aldoria',     name: 'Joias de Aldoria',  emblem: 'sword',   accent: '#2dd4bf',
    version: '1.4.0', neoforge: '26.1.4', items: 2,  blocks: 1,  recipes: 5,  edited: 'há 3 horas',   editedRank: 0 },
  { id: 'deepabyss',   name: 'Abismo Profundo',   emblem: 'ore',     accent: '#5fa9ff',
    version: '0.8.2', neoforge: '26.1.4', items: 7,  blocks: 12, recipes: 9,  edited: 'ontem',        editedRank: 1 },
  { id: 'arcanaforge', name: 'Fornalha Arcana',   emblem: 'molde',   accent: '#e89b46',
    version: '2.1.0', neoforge: '26.0.9', items: 14, blocks: 6,  recipes: 22, edited: 'há 5 dias',    editedRank: 2 },
  { id: 'wildgrove',   name: 'Bosque Encantado',  emblem: 'apple',   accent: '#5ce0a3',
    version: '1.0.0', neoforge: '26.1.4', items: 9,  blocks: 8,  recipes: 11, edited: 'há 2 semanas', editedRank: 3 },
  { id: 'cogwork',     name: 'Engrenagens',       emblem: 'lingote', accent: '#a78bfa',
    version: '0.3.0', neoforge: '26.1.4', items: 5,  blocks: 4,  recipes: 7,  edited: 'há 1 mês',     editedRank: 4 },
];

// kits iniciais do modal "Forjar novo mod"
const MX_TEMPLATES = [
  { id: 'blank', name: 'Vazio',               desc: 'Comece do zero, sem nada.',          emblem: 'molde'  },
  { id: 'items', name: 'Itens & Ferramentas', desc: 'Espada, ferramenta e comida-base.',  emblem: 'sword'  },
  { id: 'world', name: 'Blocos & Minérios',   desc: 'Minério, bloco e veio de geração.',  emblem: 'ore'    },
  { id: 'food',  name: 'Comidas',             desc: 'Consumíveis com nutrição pronta.',   emblem: 'apple'  },
];

// conquistas do ferreiro — got = desbloqueada. cat agrupa na tela dedicada.
const MX_ACH_CATS = [
  { id: 'create', label: 'Criação',     hint: 'Forjar conteúdo novo' },
  { id: 'master', label: 'Maestria',    hint: 'Dominar as mecânicas' },
  { id: 'launch', label: 'Lançamento',  hint: 'Levar o mod ao mundo' },
];
const MX_ACHIEVEMENTS = [
  { id: 'first_item',  name: 'Primeira Forja',    desc: 'Crie seu primeiro item.',            emblem: 'sword',    cat: 'create', xp: 40, got: true  },
  { id: 'first_block', name: 'Pedra Fundamental', desc: 'Crie seu primeiro bloco.',           emblem: 'ore',      cat: 'create', xp: 40, got: false },
  { id: 'armory',      name: 'Armeiro',           desc: 'Crie um conjunto de armadura.',      emblem: 'peitoral', cat: 'create', xp: 40, got: false },
  { id: 'bestiary',    name: 'Domador',           desc: 'Adicione uma criatura ao mundo.',    emblem: 'golem',    cat: 'create', xp: 40, got: false },
  { id: 'naturalist',  name: 'Naturalista',       desc: 'Gere um minério ou árvore no mundo.', emblem: 'folhas',  cat: 'create', xp: 40, got: false },
  { id: 'ore_baron',   name: 'Barão do Minério',  desc: 'Adicione 10 blocos a um mod.',       emblem: 'ore',      cat: 'master', xp: 60, got: true  },
  { id: 'ten_recipes', name: 'Mestre Cuca',       desc: 'Crie 10 receitas de crafting.',      emblem: 'apple',    cat: 'master', xp: 60, got: true  },
  { id: 'smith',       name: 'Ferreiro de Elite', desc: 'Use a Bancada de Ferreiro.',         emblem: 'molde',    cat: 'master', xp: 50, got: true  },
  { id: 'artist',      name: 'Artista de Pixel',  desc: 'Pinte sua própria textura.',        emblem: 'vara',     cat: 'master', xp: 50, got: false },
  { id: 'streak7',     name: 'Semana de Fogo',    desc: 'Forje por 7 dias seguidos.',         emblem: 'rubi',     cat: 'master', xp: 80, got: false },
  { id: 'tested',      name: 'Hora do Show',      desc: 'Teste um mod dentro do jogo.',       emblem: 'tabua',    cat: 'launch', xp: 50, got: false },
  { id: 'published',   name: 'Publicado!',        desc: 'Exporte um mod compilado.',          emblem: 'lingote',  cat: 'launch', xp: 80, got: false },
];

// ranks por XP (xp = conteúdo forjado ×8 + conquistas ×40)
const MX_RANKS = [
  { min: 0,    title: 'Aprendiz' },
  { min: 150,  title: 'Artesão' },
  { min: 400,  title: 'Ferreiro' },
  { min: 800,  title: 'Forjador' },
  { min: 1400, title: 'Mestre Ferreiro' },
  { min: 2200, title: 'Lenda da Forja' },
];

function mxComputeXp() {
  const projs = window.mxLoadProjects ? window.mxLoadProjects() : MX_PROJECTS;
  const content = projs.reduce((s, p) => s + (p.items || 0) + (p.blocks || 0) + (p.recipes || 0), 0);
  const got = window.mxAchUnlocked ? window.mxAchUnlocked().size : MX_ACHIEVEMENTS.filter(a => a.got).length;
  let bonus = 0;
  try { bonus = Number(localStorage.getItem('modrix_xp_v1') || 0) || 0; } catch (e) {}
  const xp = content * 8 + got * 40 + bonus;
  let rank = MX_RANKS[0], next = null;
  for (let i = 0; i < MX_RANKS.length; i++) {
    if (xp >= MX_RANKS[i].min) { rank = MX_RANKS[i]; next = MX_RANKS[i + 1] || null; }
  }
  const level = Math.floor(xp / 100) + 1;
  const pct = next ? Math.round(((xp - rank.min) / (next.min - rank.min)) * 100) : 100;
  return { xp, level, rank, next, pct, content, got };
}

Object.assign(window, { MX_PROFILE, MX_PROJECTS, MX_TEMPLATES, MX_ACHIEVEMENTS, MX_ACH_CATS, MX_RANKS, mxComputeXp });
