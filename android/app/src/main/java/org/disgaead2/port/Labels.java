package org.disgaead2.port;

import java.util.HashMap;
import java.util.Map;

/** Translates the cheat engine's English field labels (the core sends them in English). Stat abbreviations (ATK, HP...) stay as they are. */
final class Labels {
    private Labels() {}

    private static final Map<String, String[]> WORDS = new HashMap<>();   // English -> {pt, es, ja}

    private static void w(String en, String pt, String es, String ja) { WORDS.put(en, new String[]{pt, es, ja}); }

    static {
        w("Bonus Gauge", "Barra de bônus", "Barra de bonificación", "ボーナスゲージ");
        w("Bonus Gauge progress", "Progresso da barra de bônus", "Progreso de la barra de bonificación", "ボーナスゲージの進行度");
        w("Cheat Shop CP", "Loja de cheats: CP", "Tienda de trucos: CP", "チートショップ: CP");
        w("Cheat Shop EXP %", "Loja de cheats: EXP %", "Tienda de trucos: EXP %", "チートショップ: 経験値 %");
        w("Cheat Shop HL %", "Loja de cheats: HL %", "Tienda de trucos: HL %", "チートショップ: HL %");
        w("Cheat Shop Mana %", "Loja de cheats: Mana %", "Tienda de trucos: Mana %", "チートショップ: マナ %");
        w("Cheat Shop Skill EXP %", "Loja de cheats: EXP de habilidade %", "Tienda de trucos: EXP de habilidad %", "チートショップ: スキル経験値 %");
        w("Cheat Shop Weapon mastery %", "Loja de cheats: maestria de arma %", "Tienda de trucos: maestría de arma %", "チートショップ: 武器熟練度 %");
        w("Class ID", "ID da classe", "ID de clase", "クラスID");
        w("Counters", "Contra-ataques", "Contraataques", "カウンター");
        w("Counters (base)", "Contra-ataques (base)", "Contraataques (base)", "カウンター（基本）");
        w("Critical %", "Crítico %", "Crítico %", "クリティカル %");
        w("Critical % (base)", "Crítico % (base)", "Crítico % (base)", "クリティカル %（基本）");
        w("EXP", "EXP", "EXP", "経験値");
        w("Item ID", "ID do item", "ID del objeto", "アイテムID");
        w("Add item ID", "Adicionar item (ID)", "Añadir objeto (ID)", "アイテムを追加（ID）");
        w("Item World level limit", "Limite de nível do Mundo do Item", "Límite de nivel del Mundo de Objetos", "アイテムワールドのレベル上限");
        w("Item level", "Nível do item", "Nivel del objeto", "アイテムレベル");
        w("Jump", "Salto", "Salto", "ジャンプ");
        w("Jump (base)", "Salto (base)", "Salto (base)", "ジャンプ（基本）");
        w("Level", "Nível", "Nivel", "レベル");
        w("Mana", "Mana", "Maná", "マナ");
        w("Max HP", "HP máx.", "PV máx.", "最大HP");
        w("Max SP", "SP máx.", "PE máx.", "最大SP");
        w("Movement", "Movimento", "Movimiento", "移動力");
        w("Movement (base)", "Movimento (base)", "Movimiento (base)", "移動力（基本）");
        w("Range", "Alcance", "Alcance", "射程");
        w("Throw range (base)", "Alcance de arremesso (base)", "Alcance de lanzamiento (base)", "投げる距離（基本）");
        w("Rarity (0 common / 1 rare / 2 legendary)", "Raridade (0 comum / 1 raro / 2 lendário)", "Rareza (0 común / 1 raro / 2 legendario)", "レア度（0 通常 / 1 レア / 2 伝説）");
        w("Rarity value", "Valor de raridade", "Valor de rareza", "レア度の値");
    }

    /** lang: "pt", "es", "ja" or anything else (kept in English). */
    static String tr(String en, String lang) {
        int i = "pt".equals(lang) ? 0 : "es".equals(lang) ? 1 : "ja".equals(lang) ? 2 : -1;
        if (i < 0) return en;
        String[] hit = WORDS.get(en);
        if (hit != null) return hit[i];
        if (en.endsWith(" aptitude %")) {
            String stat = en.substring(0, en.length() - " aptitude %".length());
            return i == 0 ? "Aptidão " + stat + " %" : i == 1 ? "Aptitud " + stat + " %" : stat + "適性 %";
        }
        if (en.startsWith("Base ")) {
            String stat = en.substring(5);
            return i == 2 ? "基本" + stat : stat + " base";
        }
        return en;   // HL, HP, SP, ATK, DEF, INT, RES, HIT, SPD...: same in every language
    }
}
