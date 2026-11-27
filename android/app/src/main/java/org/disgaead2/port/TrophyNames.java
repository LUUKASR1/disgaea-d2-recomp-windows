package org.disgaead2.port;

/** Trophy names and grades come only from the player's own TROPHY.TRP; nothing from the game is shipped. */
final class TrophyNames {
    private TrophyNames() {}

    private static final String[] LIST = {};

    static boolean has(int id) { return id >= 0 && id < LIST.length; }
    static String grade(int id) { return has(id) ? LIST[id].substring(0, 1) : ""; }
    static String name(int id) { return has(id) ? LIST[id].substring(2) : ""; }
}
