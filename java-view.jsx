/* ─────────────────────────────────────────────────────────────────────────
   Modrix — "Ver Java": modal com o código NeoForge gerado a partir do mod.
   Arquivos: <Mod>.java (entry), ModItems, ModBlocks, ModRecipeProvider.
   Highlight leve de sintaxe feito com um tokenizador de regex.
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useJvState } = React;

function jvPascal(id) {
  return String(id || 'meu_mod').split(/[^a-z0-9]+/i).filter(Boolean)
    .map(w => w[0].toUpperCase() + w.slice(1)).join('');
}
function jvConst(id) { return String(id || '').toUpperCase(); }

/* registro "minecraft:stick" → Items.STICK · "aldoria:rubi" → ModItems.RUBI.get() */
function jvRegRef(reg) {
  const [ns, p] = String(reg || '').split(':');
  if (ns === 'minecraft') return `Items.${jvConst(p)}`;
  return `ModItems.${jvConst(p)}.get()`;
}

/* ── Geradores ─────────────────────────────────────────────────────────── */
function jvEntry(mod, cls) {
  const lines = [
    `package com.modrix.${mod.modId};`, '',
    'import net.neoforged.bus.api.IEventBus;',
    'import net.neoforged.fml.common.Mod;', '',
    `@Mod(${cls}.MOD_ID)`,
    `public class ${cls} {`,
    `    public static final String MOD_ID = "${mod.modId}";`, '',
    `    public ${cls}(IEventBus modEventBus) {`,
  ];
  if ((mod.items || []).length) lines.push('        ModItems.ITEMS.register(modEventBus);');
  if (!(mod.items || []).length && (mod.tools || []).length) lines.push('        ModItems.ITEMS.register(modEventBus);');
  if ((mod.blocks || []).length) lines.push('        ModBlocks.BLOCKS.register(modEventBus);');
  lines.push('    }', '}');
  return lines.join('\n');
}

function jvItemProps(it) {
  const p = [];
  if (it.consumable === 'food') p.push(`.food(new FoodProperties.Builder().nutrition(${it.nutrition || 0}).saturationModifier(${it.saturation || 0}f).build())`);
  if (it.stackSize && it.stackSize !== 64 && it.durability <= 0) p.push(`.stacksTo(${it.stackSize})`);
  if (it.durability > 0 && !(it.attackDamage > 0)) p.push(`.durability(${it.durability})`);
  if (it.rarity && it.rarity !== 'COMMON') p.push(`.rarity(Rarity.${it.rarity})`);
  if (it.fireResistant) p.push('.fireResistant()');
  return p;
}
function jvItems(mod, cls) {
  const out = [
    `package com.modrix.${mod.modId};`, '',
    'import net.minecraft.world.item.*;',
    'import net.neoforged.neoforge.registries.DeferredItem;',
    'import net.neoforged.neoforge.registries.DeferredRegister;', '',
    'public class ModItems {',
    '    public static final DeferredRegister.Items ITEMS =',
    `            DeferredRegister.createItems(${cls}.MOD_ID);`, '',
  ];
  for (const it of (mod.items || [])) {
    const C = jvConst(it.id);
    out.push(`    // ${it.name}`);
    if (it.attackDamage > 0) {
      out.push(
        `    public static final DeferredItem<SwordItem> ${C} = ITEMS.register("${it.id}",`,
        `            () -> new SwordItem(ModToolTiers.${jvConst(mod.modId)}, new Item.Properties()`,
        `                    .attributes(SwordItem.createAttributes(ModToolTiers.${jvConst(mod.modId)}, ${it.attackDamage}, ${it.attackSpeed}f))`,
        `                    .durability(${it.durability || 250})${it.rarity !== 'COMMON' ? `.rarity(Rarity.${it.rarity})` : ''}));`, '');
    } else {
      const props = jvItemProps(it);
      if (props.length === 0) {
        out.push(`    public static final DeferredItem<Item> ${C} = ITEMS.registerSimpleItem("${it.id}");`, '');
      } else {
        out.push(
          `    public static final DeferredItem<Item> ${C} = ITEMS.registerSimpleItem("${it.id}",`,
          `            new Item.Properties()${props.join('')});`, '');
      }
    }
  }
  const TOOL_CLS = { pickaxe: 'PickaxeItem', axe: 'AxeItem', shovel: 'ShovelItem', hoe: 'HoeItem' };
  for (const tl of (mod.tools || [])) {
    const C = jvConst(tl.id);
    const cls = TOOL_CLS[tl.toolType] || 'PickaxeItem';
    const tier = tl.tier === 'mod' ? `ModToolTiers.${jvConst(mod.modId)}` : `Tiers.${jvConst(tl.tier)}`;
    out.push(`    // ${tl.name}`);
    out.push(
      `    public static final DeferredItem<${cls}> ${C} = ITEMS.register("${tl.id}",`,
      `            () -> new ${cls}(${tier}, new Item.Properties()`,
      `                    .attributes(${cls}.createAttributes(${tier}, ${tl.attackDamage}F, ${tl.attackSpeed}F))`,
      `                    .durability(${tl.durability})${tl.rarity && tl.rarity !== 'COMMON' ? `.rarity(Rarity.${tl.rarity})` : ''}));`, '');
  }
  out.push('}');
  return out.join('\n');
}

function jvBlocks(mod, cls) {
  const out = [
    `package com.modrix.${mod.modId};`, '',
    'import net.minecraft.util.valueproviders.UniformInt;',
    'import net.minecraft.world.level.block.*;',
    'import net.minecraft.world.level.block.state.BlockBehaviour;',
    'import net.neoforged.neoforge.registries.DeferredBlock;',
    'import net.neoforged.neoforge.registries.DeferredRegister;', '',
    'public class ModBlocks {',
    '    public static final DeferredRegister.Blocks BLOCKS =',
    `            DeferredRegister.createBlocks(${cls}.MOD_ID);`, '',
  ];
  for (const b of (mod.blocks || [])) {
    const C = jvConst(b.id);
    const xp = b.xpMax > 0;
    out.push(`    // ${b.name}`);
    out.push(
      `    public static final DeferredBlock<${xp ? 'DropExperienceBlock' : 'Block'}> ${C} = BLOCKS.register("${b.id}",`,
      `            () -> new ${xp ? `DropExperienceBlock(UniformInt.of(${b.xpMin}, ${b.xpMax}), ` : 'Block('}BlockBehaviour.Properties.of()`,
      `                    .strength(${b.hardness}f, ${b.resistance}f)${b.requiresTool ? '.requiresCorrectToolForDrops()' : ''}${b.lightLevel > 0 ? `.lightLevel(s -> ${b.lightLevel})` : ''}));`, '');
  }
  out.push('}');
  return out.join('\n');
}

function jvRecipes(mod, cls) {
  const out = [
    `package com.modrix.${mod.modId}.datagen;`, '',
    'import net.minecraft.data.recipes.*;',
    'import net.minecraft.world.item.Items;',
    'import net.minecraft.world.item.crafting.Ingredient;', '',
    'public class ModRecipeProvider extends RecipeProvider {',
    '    @Override',
    '    protected void buildRecipes(RecipeOutput output) {',
  ];
  const ing = id => (window.MX_INGREDIENTS || []).find(x => x.id === id) || {};
  for (const r of (mod.recipes || [])) {
    let parsed = null;
    try { parsed = JSON.parse(window.genRecipeJson(r)); } catch (e) { continue; }
    out.push(`        // ${r.name}`);
    const res = jvRegRef(ing(r.result).reg || `${mod.modId}:${r.result}`);
    if (r.type === 'shaped' && parsed.pattern) {
      out.push(`        ShapedRecipeBuilder.shaped(RecipeCategory.MISC, ${res}, ${r.count || 1})`);
      parsed.pattern.forEach(row => out.push(`                .pattern("${row}")`));
      Object.entries(parsed.key || {}).forEach(([sym, v]) =>
        out.push(`                .define('${sym}', ${jvRegRef(v.item)})`));
      out.push(`                .unlockedBy("has_item", has(${res}))`, '                .save(output);', '');
    } else if (r.type === 'shapeless') {
      out.push(`        ShapelessRecipeBuilder.shapeless(RecipeCategory.MISC, ${res}, ${r.count || 1})`);
      (parsed.ingredients || []).forEach(g => out.push(`                .requires(${jvRegRef(g.item)})`));
      out.push(`                .unlockedBy("has_item", has(${res}))`, '                .save(output);', '');
    } else if (r.type === 'smelting') {
      out.push(
        `        SimpleCookingRecipeBuilder.smelting(Ingredient.of(${jvRegRef(parsed.ingredient.item)}),`,
        `                RecipeCategory.MISC, ${res}, ${r.xp || 0}f, ${r.cookTime || 200})`,
        `                .unlockedBy("has_item", has(${res})).save(output);`, '');
    } else if (r.type === 'stonecutting') {
      out.push(
        `        SingleItemRecipeBuilder.stonecutting(Ingredient.of(${jvRegRef(parsed.ingredient.item)}),`,
        `                RecipeCategory.MISC, ${res}, ${r.count || 1})`,
        `                .unlockedBy("has_item", has(${res})).save(output);`, '');
    } else if (r.type === 'smithing') {
      out.push(
        `        SmithingTransformRecipeBuilder.smithing(`,
        `                Ingredient.of(${jvRegRef(parsed.template.item)}),`,
        `                Ingredient.of(${jvRegRef(parsed.base.item)}),`,
        `                Ingredient.of(${jvRegRef(parsed.addition.item)}),`,
        `                RecipeCategory.COMBAT, ${res.replace('.get()', '.asItem()')})`,
        `                .unlocks("has_item", has(${res})).save(output, "${mod.modId}:${r.id}");`, '');
    }
  }
  out.push('    }', '}');
  return out.join('\n');
}

function genJavaFiles(mod) {
  const cls = jvPascal(mod.modId);
  const files = [{ name: `${cls}.java`, code: jvEntry(mod, cls) }];
  if ((mod.items || []).length || (mod.tools || []).length) files.push({ name: 'ModItems.java', code: jvItems(mod, cls) });
  if ((mod.blocks || []).length) files.push({ name: 'ModBlocks.java', code: jvBlocks(mod, cls) });
  if ((mod.recipes || []).length) files.push({ name: 'ModRecipeProvider.java', code: jvRecipes(mod, cls) });
  return files;
}

/* ── Highlight ─────────────────────────────────────────────────────────── */
const JV_RX = /(\/\/.*$)|("(?:[^"\\]|\\.)*")|('(?:[^'\\]|\\.)?')|\b(package|import|public|private|protected|static|final|class|new|void|return|extends|implements)\b|\b(\d+(?:\.\d+)?f?)\b|\b([A-Z][A-Za-z0-9_]*)\b/gm;
function jvHighlight(code) {
  const parts = [];
  let last = 0, m, k = 0;
  while ((m = JV_RX.exec(code)) !== null) {
    if (m.index > last) parts.push(code.slice(last, m.index));
    const cls = m[1] ? 'cmt' : (m[2] || m[3]) ? 'str' : m[4] ? 'kw' : m[5] ? 'num' : 'typ';
    parts.push(<span key={k++} className={cls}>{m[0]}</span>);
    last = m.index + m[0].length;
  }
  if (last < code.length) parts.push(code.slice(last));
  return parts;
}

/* ── Modal ─────────────────────────────────────────────────────────────── */
function JavaModal({ mod, onClose }) {
  const files = genJavaFiles(mod);
  const [cur, setCur] = useJvState(0);
  const file = files[Math.min(cur, files.length - 1)];

  return (
    <div className="mx-veil" onClick={e => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="mx-javamodal" data-screen-label="Modal Ver Java">
        <button className="mx-exp-close" onClick={onClose} title="Fechar">✕</button>
        <h2>Código Java gerado</h2>
        <div className="mx-exp-sub">src/main/java/com/modrix/{mod.modId}/ · regenerado a cada salvar</div>

        <div className="mx-jv-tabs">
          {files.map((f, i) => (
            <button key={f.name} type="button" className={`mx-jv-tab${i === cur ? ' on' : ''}`} onClick={() => setCur(i)}>
              {f.name}
            </button>
          ))}
        </div>

        <pre className="mx-code">{jvHighlight(file.code)}</pre>

        <div className="mx-jv-foot">
          Você nunca precisa editar isso — o Modrix mantém o código em sincronia com o editor visual.
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { JavaModal, genJavaFiles });
