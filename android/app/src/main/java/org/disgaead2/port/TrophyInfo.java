package org.disgaead2.port;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Reads trophy names, grades and icons from the game's own TROPHY.TRP (found in the user's game folder; nothing is shipped). */
final class TrophyInfo {
    static final class Entry {
        String name = "", grade = "";
        Bitmap icon;
    }

    private final Map<String, long[]> files = new HashMap<>();   // TRP file name -> {offset, size}
    private final Map<String, Entry> entries = new HashMap<>();
    private File trp;
    private boolean loaded;

    /** Looks for TROPHY.TRP under the game folder; false when there is none. */
    /** Forget what was read (after the player picked a TROPHY.TRP by hand). */
    void reset() { loaded = false; files.clear(); entries.clear(); trp = null; }

    int count() { return entries.size(); }

    /** gameRoot is searched at any depth; extra (a TROPHY.TRP the player imported) is the fallback. */
    boolean load(File gameRoot, File extra) {
        if (loaded) return !files.isEmpty();
        loaded = true;
        try {
            trp = find(gameRoot, 0);
            if (trp == null && extra != null && extra.isFile()) trp = extra;
            if (trp == null) return false;
            try (RandomAccessFile f = new RandomAccessFile(trp, "r")) {
                byte[] head = new byte[0x60];
                f.readFully(head);
                if (u32(head, 0) != 0x00DCA24DL) return false;
                long count = u32(head, 0x10), size = u32(head, 0x14);
                if (count <= 0 || count > 2000 || size < 0x40) return false;
                byte[] table = new byte[(int) (count * size)];
                f.readFully(table);
                for (int i = 0; i < count; i++) {
                    int o = (int) (i * size);
                    int n = 0;
                    while (n < 32 && table[o + n] != 0) n++;
                    String name = new String(table, o, n, "US-ASCII");
                    files.put(name.toUpperCase(java.util.Locale.ROOT), new long[]{u64(table, o + 0x20), u64(table, o + 0x28)});
                }
                // Names: the file of the device language first (TROP_xx.SFM), then TROP.SFM, English, and TROPCONF.SFM last.
                // The grade comes from whichever file carries it.
                java.util.List<String> order = new java.util.ArrayList<>();
                for (String code : languageCodes()) order.add("TROP_" + code + ".SFM");
                order.add("TROP.SFM");
                order.add("TROP_01.SFM");
                order.add("TROPCONF.SFM");
                for (String name : order) {
                    byte[] xml = read(f, name);
                    if (xml != null) parse(new String(xml, "UTF-8"));
                }
            }
        } catch (IOException | RuntimeException e) {
            files.clear();
        }
        return !files.isEmpty();
    }

    /** The trophy's details: from the game's TROPHY.TRP when available (name, grade, icon), otherwise the built-in list. */
    Entry get(int id) {
        Entry e = entries.get(String.format("%03d", id));
        if (e == null) e = new Entry();
        if (e.name.isEmpty()) e.name = TrophyNames.name(id);
        if (e.grade.isEmpty()) e.grade = TrophyNames.grade(id);
        if (!entries.containsKey(String.format("%03d", id)) && TrophyNames.has(id)) entries.put(String.format("%03d", id), e);
        if (e.icon == null && trp != null) {
            try (RandomAccessFile f = new RandomAccessFile(trp, "r")) {
                byte[] png = read(f, String.format("TROP%03d.PNG", id));
                if (png != null) e.icon = BitmapFactory.decodeByteArray(png, 0, png.length);
            } catch (IOException | RuntimeException ignored) { }
        }
        return e;
    }

    private void parse(String xml) {
        Matcher m = Pattern.compile("<trophy\\s+([^>/]*(?:\"[^\"]*\"[^>/]*)*)>(.*?)</trophy>", Pattern.DOTALL).matcher(xml);
        while (m.find()) {
            String attrs = m.group(1), body = m.group(2);
            Matcher id = Pattern.compile("\\bid=\"(\\d+)\"").matcher(attrs);
            if (!id.find()) continue;
            String key = String.format("%03d", Integer.parseInt(id.group(1)));
            Entry e = entries.get(key);
            if (e == null) { e = new Entry(); entries.put(key, e); }
            Matcher t = Pattern.compile("\\bttype=\"(\\w)\"").matcher(attrs);
            if (t.find() && e.grade.isEmpty()) e.grade = t.group(1);
            Matcher n = Pattern.compile("<name[^>]*>(.*?)</name>", Pattern.DOTALL).matcher(body);
            if (n.find() && e.name.isEmpty()) e.name = clean(n.group(1));
        }
    }

    private static String clean(String raw) {
        return raw.replace("<![CDATA[", "").replace("]]>", "").replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
                .replace("&quot;", "\"").replace("&apos;", "'").trim();
    }

    /** TROP_xx.SFM numbers of the PS3 languages, best match for the device language first. */
    private static String[] languageCodes() {
        switch (java.util.Locale.getDefault().getLanguage()) {
            case "pt": return new String[]{"17", "07"};
            case "es": return new String[]{"03"};
            case "fr": return new String[]{"02"};
            case "de": return new String[]{"04"};
            case "it": return new String[]{"05"};
            case "ja": return new String[]{"00"};
            case "en": return new String[]{"01", "18"};
            default: return new String[]{};
        }
    }

    int namedCount() { int n = 0; for (Entry e : entries.values()) if (!e.name.isEmpty()) n++; return n; }

    /** Names of the files inside the TRP (for diagnostics). */
    String fileList() { return files.keySet().toString(); }

    private byte[] read(RandomAccessFile f, String name) throws IOException {
        long[] at = files.get(name);
        if (at == null || at[1] <= 0 || at[1] > 8L * 1024 * 1024) return null;
        byte[] data = new byte[(int) at[1]];
        f.seek(at[0]);
        f.readFully(data);
        return data;
    }

    private static File find(File dir, int depth) {
        File[] list = dir.listFiles();
        if (list == null || depth > 6) return null;
        for (File c : list) if (c.isFile() && c.getName().equalsIgnoreCase("TROPHY.TRP")) return c;
        for (File c : list) if (c.isDirectory()) { File r = find(c, depth + 1); if (r != null) return r; }
        return null;
    }

    private static long u32(byte[] b, int o) {
        return ((b[o] & 0xFFL) << 24) | ((b[o + 1] & 0xFFL) << 16) | ((b[o + 2] & 0xFFL) << 8) | (b[o + 3] & 0xFFL);
    }

    private static long u64(byte[] b, int o) { return (u32(b, o) << 32) | u32(b, o + 4); }

    // ---- Pulling TROPHY.TRP out of the player's own game disc image (plain ISO 9660, as the PS3 disc dumps are) ----

    /** Copies PS3_GAME/TROPDIR/&lt;any&gt;/TROPHY.TRP from the ISO into out. False when the ISO has no such file or is not readable. */
    static boolean extractFromIso(android.content.ContentResolver resolver, android.net.Uri uri, File out) {
        try (android.os.ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "r");
             java.io.FileInputStream in = new java.io.FileInputStream(pfd.getFileDescriptor())) {
            java.nio.channels.FileChannel ch = in.getChannel();
            byte[] pvd = readAt(ch, 16L * 2048, 2048);
            if (pvd == null || pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1') return false;
            long[] dir = {le32(pvd, 156 + 2), le32(pvd, 156 + 10)};   // extent, size of the root directory
            for (String part : new String[]{"PS3_GAME", "TROPDIR"}) {
                dir = find(ch, dir, part, true);
                if (dir == null) return false;
            }
            long[] sub = find(ch, dir, null, true);                    // the NPWRxxxxx_00 folder
            if (sub == null) return false;
            long[] file = find(ch, sub, "TROPHY.TRP", false);
            if (file == null || file[1] <= 0x60 || file[1] > 64L * 1024 * 1024) return false;
            ch.position(file[0] * 2048);
            try (java.io.OutputStream os = new java.io.FileOutputStream(out)) {
                byte[] buf = new byte[65536];
                long left = file[1];
                while (left > 0) {
                    int n = in.read(buf, 0, (int) Math.min(buf.length, left));
                    if (n <= 0) return false;
                    os.write(buf, 0, n);
                    left -= n;
                }
            }
            return true;
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    /** Looks in a directory for an entry: by name (any when name == null, first match of the wanted kind). Returns {extent, size}. */
    private static long[] find(java.nio.channels.FileChannel ch, long[] dir, String name, boolean wantDir) throws IOException {
        byte[] data = readAt(ch, dir[0] * 2048, (int) Math.min(dir[1], 1L << 20));
        if (data == null) return null;
        int o = 0;
        while (o < data.length) {
            int len = data[o] & 0xFF;
            if (len == 0) { o = (o / 2048 + 1) * 2048; continue; }   // records never straddle sectors
            if (o + len > data.length) break;
            int flags = data[o + 25] & 0xFF, nameLen = data[o + 32] & 0xFF;
            boolean isDir = (flags & 2) != 0;
            if (nameLen > 0 && isDir == wantDir && !(nameLen == 1 && (data[o + 33] == 0 || data[o + 33] == 1))) {
                String n = new String(data, o + 33, nameLen, "US-ASCII");
                int semi = n.indexOf(';');
                if (semi >= 0) n = n.substring(0, semi);
                if (name == null || n.equalsIgnoreCase(name)) return new long[]{le32(data, o + 2), le32(data, o + 10)};
            }
            o += len;
        }
        return null;
    }

    private static byte[] readAt(java.nio.channels.FileChannel ch, long pos, int len) throws IOException {
        java.nio.ByteBuffer b = java.nio.ByteBuffer.allocate(len);
        ch.position(pos);
        while (b.hasRemaining()) if (ch.read(b) < 0) break;
        return b.position() == 0 ? null : b.array();
    }

    private static long le32(byte[] b, int o) {
        return (b[o] & 0xFFL) | ((b[o + 1] & 0xFFL) << 8) | ((b[o + 2] & 0xFFL) << 16) | ((b[o + 3] & 0xFFL) << 24);
    }
}
