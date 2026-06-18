/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Vitrine: como o mod se apresenta ao mundo quando publicado.
   Página de lançamento ORIGINAL gerada 100% do conteúdo real do projeto:
   emblema + descrição, galeria com a pixel-art de cada registro, raridades
   e o histórico de versões. O complemento natural do botão Publicar.
   ───────────────────────────────────────────────────────────────────────── */

const SH_RAR = { COMMON: 'Comum', UNCOMMON: 'Incomum', RARE: 'Raro', EPIC: 'Épico' };
const SH_RAR_CSS = { COMMON: 'var(--rar-common)', UNCOMMON: 'var(--rar-uncommon)', RARE: 'var(--rar-rare)', EPIC: 'var(--rar-epic)' };
const SH_RTYPE = {
  shaped: 'Bancada', shapeless: 'Bancada', smelting: 'Fornalha', blasting: 'Alto-forno',
  smoking: 'Defumador', stonecutting: 'Cortador', smithing: 'Bancada de ferreiro', campfire: 'Fogueira',
};
const SH_TOOLT = { pickaxe: 'Picareta', axe: 'Machado', shovel: 'Pá', hoe: 'Enxada', sword: 'Espada' };
const SH_MOBCAT = { hostile: 'Hostil', neutral: 'Neutro', passive: 'Pacífico' };
const SH_FXCAT = { beneficial: 'Benéfico', harmful: 'Maléfico', neutral: 'Neutro' };

function shMeta(kind, r) {
  if (kind === 'item') return r.consumable === 'food' ? `Comida · +${r.nutrition || 0} de fome` : (r.attackDamage > 0 ? `${r.attackDamage} de dano` : 'Item');
  if (kind === 'block') return `Dureza ${r.hardness}${r.drops ? ` · solta ${r.drops}` : ''}`;
  if (kind === 'tool') return `${SH_TOOLT[r.toolType] || 'Ferramenta'} · ${r.durability} usos`;
  if (kind === 'armor') return `${(r.pieces || []).length} peças · resistência ${r.toughness}`;
  if (kind === 'recipe') return SH_RTYPE[r.type] || 'Receita';
  if (kind === 'effect') return `${SH_FXCAT[r.category] || 'Status'} · ${r.duration}s`;
  if (kind === 'mob') return `${SH_MOBCAT[r.category] || 'Mob'} · ${r.health} HP`;
  if (kind === 'ore') return `Y ${r.minY}–${r.maxY} · ${r.dimension}`;
  if (kind === 'tree') return `${r.minHeight}–${r.maxHeight} blocos de altura`;
  return '';
}

function ShowcasePage({ mod, iconStyle }) {
  // emblema: o escolhido, ou o primeiro sprite que houver
  const firstSprite = (() => {
    for (const k of ['items', 'tools', 'blocks', 'armor', 'mobs']) {
      const a = mod[k] || []; if (a.length && a[0].sprite) return a[0].sprite;
    }
    return 'sword';
  })();
  const emblem = mod.emblem || firstSprite;

  const groups = [
    { key: 'items', kind: 'item', label: 'Itens' },
    { key: 'tools', kind: 'tool', label: 'Ferramentas' },
    { key: 'armor', kind: 'armor', label: 'Armaduras' },
    { key: 'blocks', kind: 'block', label: 'Blocos' },
    { key: 'mobs', kind: 'mob', label: 'Criaturas' },
    { key: 'effects', kind: 'effect', label: 'Efeitos' },
    { key: 'ores', kind: 'ore', label: 'Geração no mundo' },
    { key: 'trees', kind: 'tree', label: 'Árvores' },
    { key: 'recipes', kind: 'recipe', label: 'Receitas' },
  ].map(g => ({ ...g, list: mod[g.key] || [] })).filter(g => g.list.length);

  const counts = [
    { n: (mod.items || []).length, label: 'itens' },
    { n: (mod.blocks || []).length, label: 'blocos' },
    { n: (mod.tools || []).length, label: 'ferramentas' },
    { n: (mod.armor || []).length, label: 'armaduras' },
    { n: (mod.mobs || []).length, label: 'criaturas' },
    { n: (mod.recipes || []).length, label: 'receitas' },
  ].filter(c => c.n > 0);

  const changelog = mod.changelog || [];
  const latest = changelog[0];
  const totalReg = groups.reduce((s, g) => s + g.list.length, 0);
  const empty = totalReg === 0;

  return (
    <div className="sh-page" data-screen-label="Vitrine">
      <div className="sh-inner">
        {/* ── topo: como o mundo vê ── */}
        <div className="sh-eyebrow">
          <window.Icon name="spark" style={iconStyle} size={13} /> Pré-visualização da página de lançamento
        </div>

        {/* ── hero ── */}
        <header className="sh-hero">
          <div className="sh-hero-emblem"><window.PixelSprite sprite={emblem} size={84} glint /></div>
          <div className="sh-hero-body">
            <div className="sh-hero-id">{mod.modId} · v{mod.modVersion}</div>
            <h1 className="sh-hero-name">{mod.modName}</h1>
            <p className="sh-hero-desc">{mod.description || 'Um mod forjado no Modrix — pronto pra entrar no seu mundo.'}</p>
            <div className="sh-hero-meta">
              <span className="sh-by">por <b>{mod.author || 'autor desconhecido'}</b></span>
              <span className="sh-badge"><span className="mx-live-dot" /> NeoForge {mod.neoforge}</span>
              <span className="sh-badge">Java 21</span>
            </div>
            {counts.length > 0 && (
              <div className="sh-counts">
                {counts.map((c, i) => (
                  <span className="sh-count" key={i}><b>{c.n}</b> {c.label}</span>
                ))}
              </div>
            )}
          </div>
        </header>

        {empty ? (
          <div className="sh-empty">
            <div className="mx-empty-slot art"><window.Icon name="spark" style={iconStyle} size={32} /></div>
            <h2>Nada pra mostrar… ainda</h2>
            <p>Crie alguns itens, blocos ou um conjunto pronto e sua vitrine vai se montar sozinha aqui.</p>
          </div>
        ) : (
          <>
            {/* ── novidades da versão atual ── */}
            {latest && (latest.notes || '').trim() && (
              <section className="sh-news">
                <div className="sh-news-tag">Novidades · v{latest.version}</div>
                <p>{latest.notes}</p>
              </section>
            )}

            {/* ── galeria de conteúdo ── */}
            {groups.map(g => (
              <section className="sh-sect" key={g.key}>
                <div className="sh-sect-head">
                  <h2>{g.label}</h2>
                  <span className="sh-sect-count">{g.list.length}</span>
                </div>
                <div className="sh-grid">
                  {g.list.map(r => {
                    const rarCss = r.rarity ? SH_RAR_CSS[r.rarity] : null;
                    return (
                      <div className="sh-card" key={r.id} style={rarCss ? { '--rar': rarCss } : null}>
                        <div className={`sh-card-art${rarCss ? ' rar' : ''}`}>
                          <window.PixelSprite sprite={r.sprite} size={42} glint={r.rarity === 'EPIC'} />
                        </div>
                        <div className="sh-card-body">
                          <div className="sh-card-name">{r.name}</div>
                          <div className="sh-card-meta">{shMeta(g.kind, r)}</div>
                        </div>
                        {r.rarity && r.rarity !== 'COMMON' && (
                          <span className="sh-rar" style={{ color: rarCss }}>{SH_RAR[r.rarity]}</span>
                        )}
                      </div>
                    );
                  })}
                </div>
              </section>
            ))}

            {/* ── histórico de versões ── */}
            {changelog.length > 0 && (
              <section className="sh-sect">
                <div className="sh-sect-head"><h2>Histórico de versões</h2></div>
                <div className="sh-log">
                  {changelog.map((c, i) => (
                    <div className={`sh-log-row${i === 0 ? ' top' : ''}`} key={i}>
                      <div className="sh-log-ver">v{c.version}<span className="sh-log-date">{c.date}</span></div>
                      <div className="sh-log-notes">{c.notes}</div>
                    </div>
                  ))}
                </div>
              </section>
            )}
          </>
        )}

        {/* ── rodapé ── */}
        <footer className="sh-foot">
          <span>{mod.modId} · v{mod.modVersion}</span>
          <span>·</span>
          <span>requer NeoForge {mod.neoforge} · Java 21</span>
          <span>·</span>
          <span>forjado no Modrix</span>
        </footer>
      </div>
    </div>
  );
}

Object.assign(window, { ShowcasePage });
