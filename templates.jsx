/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Biblioteca de Conjuntos (content packs).
   Cada pack monta vários registros já encadeados (bloco → item → geração…)
   a partir de um nome de material. Os ids derivam de toId(material) para
   manter as referências cruzadas íntegras.
   ───────────────────────────────────────────────────────────────────────── */

// Cada pack: build(material, idBase) → [{ key, rec }] (sem _new; o app carimba).
const MX_PACKS = [
  {
    id: 'ore_full',
    name: 'Minério completo',
    desc: 'Bloco de minério, o recurso bruto e a geração no subsolo — prontos para minerar.',
    accent: '#7b8290',
    sprites: ['ore', 'rubi', 'ore'],
    summary: m => [`Bloco: Minério de ${m}`, `Item: ${m} Bruto`, `Geração: Veio de ${m}`],
    build: (m, id) => [
      { key: 'blocks', rec: { kind: 'block', id: `minerio_de_${id}`, name: `Minério de ${m}`, sprite: 'ore',
        hardness: 3, resistance: 3, requiresTool: true, tool: 'pickaxe', toolTier: 'iron', lightLevel: 0,
        drops: `${id}_bruto`, xpMin: 2, xpMax: 5 } },
      { key: 'items', rec: { kind: 'item', id: `${id}_bruto`, name: `${m} Bruto`, sprite: 'rubi',
        stackSize: 64, durability: 0, rarity: 'UNCOMMON', fireResistant: false, attackDamage: 0, attackSpeed: 0,
        consumable: 'none', enchantable: false } },
      { key: 'ores', rec: { kind: 'ore', id: `veio_de_${id}`, name: `Veio de ${m}`, sprite: 'ore',
        block: `minerio_de_${id}`, veinSize: 7, perChunk: 5, minY: -54, maxY: 24, dimension: 'overworld', deepslate: true } },
    ],
  },
  {
    id: 'tool_kit',
    name: 'Kit de ferramentas',
    desc: 'As cinco ferramentas clássicas no mesmo material e nível — picareta, machado, pá, enxada e espada.',
    accent: '#c79a63',
    sprites: ['picareta', 'sword'],
    summary: m => [`Picareta de ${m}`, `Machado de ${m}`, `Pá de ${m}`, `Enxada de ${m}`, `Espada de ${m}`],
    build: (m, id) => {
      const base = { tier: 'mod', durability: 780, speed: 7.5, attackSpeed: -2.8, rarity: 'UNCOMMON',
        stackSize: 1, consumable: 'none', enchantable: true, fireResistant: false };
      const tool = (t, label, sprite, dmg, sp, as) => ({ kind: 'tool', id: `${t}_de_${id}`, name: `${label} de ${m}`,
        sprite, toolType: t, ...base, speed: sp, attackDamage: dmg, attackSpeed: as });
      return [
        { key: 'tools', rec: tool('pickaxe', 'Picareta', 'picareta', 1, 7.5, -2.8) },
        { key: 'tools', rec: tool('axe', 'Machado', 'picareta', 6, 7, -3.0) },
        { key: 'tools', rec: tool('shovel', 'Pá', 'picareta', 1.5, 7.5, -3.0) },
        { key: 'tools', rec: tool('hoe', 'Enxada', 'picareta', 0, 7.5, -1.0) },
        { key: 'tools', rec: tool('sword', 'Espada', 'sword', 6, 7.5, -2.4) },
      ];
    },
  },
  {
    id: 'armor_set',
    name: 'Conjunto de armadura',
    desc: 'A armadura completa de quatro peças mais o lingote que serve de material de fabricação.',
    accent: '#e23b5b',
    sprites: ['capacete', 'peitoral', 'calcas', 'botas'],
    summary: m => [`Armadura de ${m} (4 peças)`, `Lingote de ${m}`],
    build: (m, id) => [
      { key: 'armor', rec: { kind: 'armor', id: `armadura_de_${id}`, name: `Armadura de ${m}`, sprite: 'peitoral',
        pieces: ['helmet', 'chestplate', 'leggings', 'boots'], durabilityMult: 28, toughness: 2, knockbackRes: 0,
        enchantability: 14, rarity: 'RARE' } },
      { key: 'items', rec: { kind: 'item', id: `lingote_de_${id}`, name: `Lingote de ${m}`, sprite: 'lingote',
        stackSize: 64, durability: 0, rarity: 'UNCOMMON', fireResistant: false, attackDamage: 0, attackSpeed: 0,
        consumable: 'none', enchantable: false } },
    ],
  },
  {
    id: 'mob_loot',
    name: 'Criatura com tesouro',
    desc: 'Um mob hostil que surge no escuro e larga um item exclusivo ao ser derrotado.',
    accent: '#5b8f4e',
    sprites: ['golem', 'rubi'],
    summary: m => [`Mob: Guardião de ${m}`, `Drop: Fragmento de ${m}`],
    build: (m, id) => [
      { key: 'items', rec: { kind: 'item', id: `fragmento_de_${id}`, name: `Fragmento de ${m}`, sprite: 'rubi',
        stackSize: 64, durability: 0, rarity: 'RARE', fireResistant: false, attackDamage: 0, attackSpeed: 0,
        consumable: 'none', enchantable: false } },
      { key: 'mobs', rec: { kind: 'mob', id: `guardiao_de_${id}`, name: `Guardião de ${m}`, sprite: 'golem',
        category: 'hostile', health: 30, damage: 5, speed: 0.25, knockbackRes: 0.4, armor: 4, xpDrop: 10,
        dropItem: `fragmento_de_${id}`, dropMin: 1, dropMax: 3, spawns: true, spawnLight: 7 } },
    ],
  },
];

// ═══ Galeria (modal) ════════════════════════════════════════════════════════
function TemplateGallery({ onClose, onAdd }) {
  const { useState: useTgState } = React;
  const [material, setMaterial] = useTgState('Safira');
  const mat = material.trim() || 'Safira';

  return (
    <div className="tg-veil" onClick={e => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="tg-modal" data-screen-label="Conjuntos prontos">
        <button className="tg-close" onClick={onClose} title="Fechar">✕</button>
        <div className="tg-head">
          <div className="tg-head-ico"><window.Icon name="grid" style="line" size={20} /></div>
          <div>
            <h2>Conjuntos prontos</h2>
            <p>Adicione vários registros já encadeados de uma vez. Dê o nome do material — o resto se monta sozinho.</p>
          </div>
        </div>

        <div className="tg-material">
          <label className="mx-label" style={{ margin: 0 }}>Nome do material</label>
          <input className="mx-input tg-input" value={material} onChange={e => setMaterial(e.target.value)}
            placeholder="Safira" maxLength={24} />
          <span className="tg-material-hint">vira <code>{window.toId ? window.toId(mat) : mat}</code> nos ids</span>
        </div>

        <div className="tg-grid">
          {MX_PACKS.map(p => {
            const lines = p.summary(mat);
            return (
              <div className="tg-card" key={p.id}>
                <div className="tg-card-top">
                  <div className="tg-sprites" style={{ '--pk': p.accent }}>
                    {p.sprites.map((s, i) => (
                      <span className="tg-sprite" key={i} style={{ zIndex: p.sprites.length - i }}>
                        <window.PixelSprite sprite={s} size={26} />
                      </span>
                    ))}
                  </div>
                  <span className="tg-count">{lines.length} registros</span>
                </div>
                <div className="tg-card-name">{p.name}</div>
                <div className="tg-card-desc">{p.desc}</div>
                <ul className="tg-list">
                  {lines.map((l, i) => <li key={i}><span className="tg-bullet" style={{ background: p.accent }} />{l}</li>)}
                </ul>
                <button className="tg-add" onClick={() => onAdd(p, mat)}>
                  <window.Icon name="spark" style="line" size={13} /> Adicionar ao mod
                </button>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { MX_PACKS, TemplateGallery });
