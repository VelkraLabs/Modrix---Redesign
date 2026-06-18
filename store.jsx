/* ─────────────────────────────────────────────────────────────────────────
   Modrix — camada de persistência (localStorage) compartilhada entre
   o launcher e a oficina: projetos, conteúdo de cada mod e conquistas.
   Tudo é lido "lazy" (no momento da chamada) pra não depender de ordem
   de carga dos scripts.
   ───────────────────────────────────────────────────────────────────────── */

const MX_LS_PROJECTS = 'modrix_projects_v2';
const MX_LS_ACH = 'modrix_ach_v2';
const mxLsMod = id => `modrix_mod_${id}_v2`;

function mxLsGet(key) {
  try { return JSON.parse(localStorage.getItem(key) || 'null'); } catch (e) { return null; }
}
function mxLsSet(key, val) {
  try { localStorage.setItem(key, JSON.stringify(val)); } catch (e) {}
}

/* ── Projetos (lista de saves do launcher) ─────────────────────────────── */
function mxLoadProjects() {
  const v = mxLsGet(MX_LS_PROJECTS);
  if (Array.isArray(v)) return v;
  return JSON.parse(JSON.stringify(window.MX_PROJECTS || []));
}
function mxSaveProjects(list) { mxLsSet(MX_LS_PROJECTS, list); }

/* atualiza meta de um projeto (contadores, nome, "editado") e o move pro topo */
function mxTouchProject(id, patch) {
  const list = mxLoadProjects();
  const i = list.findIndex(p => p.id === id);
  if (i < 0) return;
  const p = { ...list[i], ...patch };
  list.splice(i, 1); list.unshift(p);
  mxSaveProjects(list);
}

/* ── Conteúdo completo de um mod ───────────────────────────────────────── */
function mxLoadMod(id) { return mxLsGet(mxLsMod(id)); }
function mxSaveMod(id, mod) { mxLsSet(mxLsMod(id), mod); }

/* ── Conquistas ────────────────────────────────────────────────────────── */
function mxAchUnlocked() {
  const defaults = (window.MX_ACHIEVEMENTS || []).filter(a => a.got).map(a => a.id);
  const extra = mxLsGet(MX_LS_ACH) || [];
  return new Set([...defaults, ...extra]);
}
/* desbloqueia; retorna o objeto da conquista se for inédita, senão null */
function mxUnlockAch(id) {
  const ach = (window.MX_ACHIEVEMENTS || []).find(a => a.id === id);
  if (!ach) return null;
  const unlocked = mxAchUnlocked();
  if (unlocked.has(id)) return null;
  const extra = mxLsGet(MX_LS_ACH) || [];
  extra.push(id);
  mxLsSet(MX_LS_ACH, extra);
  return ach;
}

/* ── Ofensiva (streak de dias forjando) ────────────────────────────────── */
const MX_LS_STREAK = 'modrix_streak_v1';
function mxStreakTick() {
  const today = new Date().toISOString().slice(0, 10);
  let s = mxLsGet(MX_LS_STREAK);
  if (!s || !s.last) {
    s = { last: today, count: (window.MX_PROFILE && window.MX_PROFILE.streak) || 1 };
  } else if (s.last !== today) {
    const yesterday = new Date(Date.now() - 864e5).toISOString().slice(0, 10);
    s = { last: today, count: s.last === yesterday ? s.count + 1 : 1 };
  }
  mxLsSet(MX_LS_STREAK, s);
  return s.count;
}

/* ── Atividade recente (por projeto, últimas 8 ações) ─────────────────── */
const mxLsAct = id => `modrix_act_${id}_v1`;
function mxActivity(id) { return mxLsGet(mxLsAct(id)) || []; }
function mxPushActivity(id, entry) {
  const list = [entry, ...mxActivity(id)].slice(0, 8);
  mxLsSet(mxLsAct(id), list);
  return list;
}

Object.assign(window, {
  mxLoadProjects, mxSaveProjects, mxTouchProject,
  mxLoadMod, mxSaveMod, mxAchUnlocked, mxUnlockAch, mxStreakTick,
  mxActivity, mxPushActivity,
});
