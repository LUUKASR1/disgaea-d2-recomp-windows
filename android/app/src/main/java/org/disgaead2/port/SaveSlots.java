package org.disgaead2.port;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Save slots for the PS3 savedata folder, same idea as the Windows launcher: a slot is a plain copy of the savedata
 * directory kept at <files>/save-backups/<yyyy-MM-dd_HH-mm-ss>[-label]. Loading a slot never discards the current saves
 * (they are copied into a new slot first). Nothing is converted or decrypted, and nothing may run while the game is open.
 * Saves coming from Windows/PC are imported from a folder or a .zip: every folder that holds a PARAM.SFO is a save.
 */
final class SaveSlots {
    static final String AUTO = "auto";
    static final int KEEP_AUTO = 5;
    private static final long MAX_IMPORT_BYTES = 256L * 1024 * 1024;   // a save is a few MB; refuse a whole storage by mistake

    static final class Slot {
        final File dir;
        final String name;     // folder name
        final int saves;       // number of save folders inside
        Slot(File dir, int saves) { this.dir = dir; this.name = dir.getName(); this.saves = saves; }

        /** "2026-11-20 11:40 · label" from the folder name. */
        String title() {
            String n = name;
            if (n.length() >= 19 && n.charAt(10) == '_') {
                String when = n.substring(0, 10) + " " + n.substring(11, 19).replace('-', ':').substring(0, 5);
                String rest = n.length() > 20 ? n.substring(20) : "";
                return rest.isEmpty() ? when : when + "  ·  " + rest;
            }
            return n;
        }
    }

    private SaveSlots() { }

    static File saveDir(File files) { return new File(files, "dev_hdd0/home/00000001/savedata"); }

    static File slotsDir(File files) { return new File(files, "save-backups"); }

    private static String safeLabel(String label) {
        if (label == null) return "";
        String s = label.replaceAll("[^0-9A-Za-zÀ-ÿ _-]+", "").trim().replace(' ', '-');
        return s.length() > 40 ? s.substring(0, 40) : s;
    }

    private static File unique(File base) {
        File candidate = base;
        for (int n = 2; candidate.exists(); n++) candidate = new File(base.getParentFile(), base.getName() + "-" + n);
        return candidate;
    }

    private static boolean hasContent(File dir) {
        String[] names = dir.list();
        return names != null && names.length > 0;
    }

    private static File newSlotTarget(File files, String label) {
        File root = slotsDir(files);
        root.mkdirs();
        String stamp = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(new Date());
        String l = safeLabel(label);
        return unique(new File(root, l.isEmpty() ? stamp : stamp + "-" + l));
    }

    /** Copy the current savedata into a new slot; null when there is nothing to copy. */
    static File create(File files, String label) throws IOException {
        File source = saveDir(files);
        if (!source.isDirectory() || !hasContent(source)) return null;
        File target = newSlotTarget(files, label);
        File tmp = new File(target.getParentFile(), target.getName() + ".tmp");
        deleteTree(tmp);
        copyTree(source, tmp);
        if (!tmp.renameTo(target)) throw new IOException("rename");
        return target;
    }

    static List<Slot> list(File files) {
        File[] dirs = slotsDir(files).listFiles(f -> f.isDirectory() && !f.getName().endsWith(".tmp"));
        List<Slot> out = new ArrayList<>();
        if (dirs == null) return out;
        Arrays.sort(dirs, (a, b) -> b.getName().compareTo(a.getName()));   // newest first
        for (File d : dirs) {
            File[] kids = d.listFiles(File::isDirectory);
            out.add(new Slot(d, kids == null ? 0 : kids.length));
        }
        return out;
    }

    /** Backup taken before each game launch; only the newest KEEP_AUTO are kept. */
    static void autoBackup(File files) throws IOException {
        if (create(files, AUTO) == null) return;
        int seen = 0;
        for (Slot s : list(files)) if (s.name.contains("-" + AUTO) && ++seen > KEEP_AUTO) deleteTree(s.dir);
    }

    /** Replace the current savedata with a slot. The current saves are kept first as "antes-de-carregar". */
    static void load(File files, Slot slot) throws IOException {
        if (!slot.dir.isDirectory() || !slot.dir.getParentFile().equals(slotsDir(files))) throw new IOException("slot");
        create(files, "antes-de-carregar");
        File target = saveDir(files);
        File staging = new File(target.getParentFile(), target.getName() + ".restaurando");
        deleteTree(staging);
        target.getParentFile().mkdirs();
        copyTree(slot.dir, staging);
        deleteTree(target);
        if (!staging.renameTo(target)) throw new IOException("rename");
    }

    static void delete(File files, Slot slot) throws IOException {
        if (!slot.dir.getParentFile().equals(slotsDir(files))) throw new IOException("slot");
        deleteTree(slot.dir);
    }

    // ------------------------------------------------------------------ import

    /** Import a folder picked with the system file picker (a savedata folder, one save folder, or a parent of those). */
    static int importTree(ContentResolver cr, Uri tree, String displayName, File files) throws IOException {
        File work = new File(files, "save-import.tmp");
        deleteTree(work);
        try {
            work.mkdirs();
            long[] budget = {0};
            copyDocumentTree(cr, tree, DocumentsContract.getTreeDocumentId(tree), work, 0, budget);
            return finishImport(work, displayName, files);
        } finally {
            deleteTree(work);
        }
    }

    static int importZip(ContentResolver cr, Uri zip, String displayName, File files) throws IOException {
        File work = new File(files, "save-import.tmp");
        deleteTree(work);
        try {
            work.mkdirs();
            String root = work.getCanonicalPath() + File.separator;
            long total = 0;
            try (InputStream raw = cr.openInputStream(zip); ZipInputStream in = new ZipInputStream(raw)) {
                ZipEntry e;
                byte[] buf = new byte[1 << 16];
                while ((e = in.getNextEntry()) != null) {
                    File out = new File(work, e.getName().replace('\\', '/'));
                    if (!out.getCanonicalPath().startsWith(root)) continue;   // never write outside the work folder
                    if (e.isDirectory()) { out.mkdirs(); continue; }
                    out.getParentFile().mkdirs();
                    try (OutputStream os = new FileOutputStream(out)) {
                        int n;
                        while ((n = in.read(buf)) > 0) {
                            total += n;
                            if (total > MAX_IMPORT_BYTES) throw new IOException("too big");
                            os.write(buf, 0, n);
                        }
                    }
                }
            }
            return finishImport(work, displayName, files);
        } finally {
            deleteTree(work);
        }
    }

    private static int finishImport(File work, String fallbackName, File files) throws IOException {
        List<File> found = new ArrayList<>();
        findSaves(work, 0, found);
        if (found.isEmpty()) return 0;
        String stem = fallbackName == null ? "save" : fallbackName.replaceAll("\\.[A-Za-z0-9]{1,4}$", "");
        File target = newSlotTarget(files, "importado");
        File tmp = new File(target.getParentFile(), target.getName() + ".tmp");
        deleteTree(tmp);
        tmp.mkdirs();
        for (File f : found) {
            String name = f.equals(work) ? safeLabel(stem) : f.getName();
            if (name.isEmpty()) name = "SAVE";
            copyTree(f, unique(new File(tmp, name)));
        }
        if (!tmp.renameTo(target)) throw new IOException("rename");
        return found.size();
    }

    /** A save folder is any folder that holds a PARAM.SFO; do not look inside one once found. */
    private static void findSaves(File dir, int depth, List<File> out) {
        File[] kids = dir.listFiles();
        if (kids == null) return;
        for (File k : kids) if (k.isFile() && k.getName().equalsIgnoreCase("PARAM.SFO")) { out.add(dir); return; }
        if (depth >= 5) return;
        for (File k : kids) if (k.isDirectory()) findSaves(k, depth + 1, out);
    }

    private static void copyDocumentTree(ContentResolver cr, Uri tree, String docId, File dest, int depth, long[] budget) throws IOException {
        if (depth > 12) return;
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        try (Cursor c = cr.query(children, new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (name == null || name.contains("/") || name.equals("..") || name.equals(".")) continue;
                File out = new File(dest, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    out.mkdirs();
                    copyDocumentTree(cr, tree, id, out, depth + 1, budget);
                } else {
                    try (InputStream in = cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, id));
                         OutputStream os = new FileOutputStream(out)) {
                        if (in == null) continue;
                        byte[] buf = new byte[1 << 16];
                        int n;
                        while ((n = in.read(buf)) > 0) {
                            budget[0] += n;
                            if (budget[0] > MAX_IMPORT_BYTES) throw new IOException("too big");
                            os.write(buf, 0, n);
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------ export

    /** Write a slot as a .zip (entries are "<save folder>/<file>", the same layout the import understands). */
    static void exportZip(Slot slot, OutputStream out) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            zipTree(slot.dir, "", zip);
        }
    }

    private static void zipTree(File dir, String prefix, ZipOutputStream zip) throws IOException {
        File[] kids = dir.listFiles();
        if (kids == null) return;
        Collections.sort(Arrays.asList(kids));
        byte[] buf = new byte[1 << 16];
        for (File k : kids) {
            if (k.isDirectory()) { zipTree(k, prefix + k.getName() + "/", zip); continue; }
            zip.putNextEntry(new ZipEntry(prefix + k.getName()));
            try (InputStream in = new FileInputStream(k)) {
                int n;
                while ((n = in.read(buf)) > 0) zip.write(buf, 0, n);
            }
            zip.closeEntry();
        }
    }

    // ------------------------------------------------------------------ files

    private static void copyTree(File from, File to) throws IOException {
        if (from.isDirectory()) {
            if (!to.mkdirs() && !to.isDirectory()) throw new IOException("mkdir " + to);
            File[] kids = from.listFiles();
            if (kids != null) for (File k : kids) copyTree(k, new File(to, k.getName()));
        } else {
            byte[] buf = new byte[1 << 16];
            try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(to)) {
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
        }
    }

    static void deleteTree(File f) {
        if (!f.exists()) return;
        File[] kids = f.isDirectory() ? f.listFiles() : null;
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }
}
