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
                    files.put(name, new long[]{u64(table, o + 0x20), u64(table, o + 0x28)});
                }
                byte[] conf = read(f, "TROPCONF.SFM");
                if (conf != null) parse(new String(conf, "UTF-8"));
            }
        } catch (IOException | RuntimeException e) {
            files.clear();
        }
        return !files.isEmpty();
    }

    Entry get(int id) {
        Entry e = entries.get(String.format("%03d", id));
        if (e == null || trp == null) return e;
        if (e.icon == null) {
            try (RandomAccessFile f = new RandomAccessFile(trp, "r")) {
                byte[] png = read(f, String.format("TROP%03d.PNG", id));
                if (png != null) e.icon = BitmapFactory.decodeByteArray(png, 0, png.length);
            } catch (IOException | RuntimeException ignored) { }
        }
        return e;
    }

    private void parse(String xml) {
        Matcher m = Pattern.compile("<trophy\\s+([^>]*)>(.*?)</trophy>", Pattern.DOTALL).matcher(xml);
        while (m.find()) {
            String attrs = m.group(1), body = m.group(2);
            Matcher id = Pattern.compile("\\bid=\"(\\d+)\"").matcher(attrs);
            if (!id.find()) continue;
            Entry e = new Entry();
            Matcher t = Pattern.compile("\\bttype=\"(\\w)\"").matcher(attrs);
            if (t.find()) e.grade = t.group(1);
            Matcher n = Pattern.compile("<name>(.*?)</name>", Pattern.DOTALL).matcher(body);
            if (n.find()) e.name = n.group(1).replace("<![CDATA[", "").replace("]]>", "").trim();
            entries.put(String.format("%03d", Integer.parseInt(id.group(1))), e);
        }
    }

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
}
