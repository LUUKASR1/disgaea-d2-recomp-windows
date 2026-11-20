"""Android-only tweaks applied to a COPY of the ps3recomp SDK before the core is compiled.

The workspace SDK is pinned by patches/SDK-WINDOWS.lock (changing it would force every user to prepare the game
again), so Android-specific runtime changes are applied here, at build time, on a private copy. Every edit is an
exact-text replacement; if the text is not found the edit is skipped with a warning and the build goes on unchanged."""
from pathlib import Path

FAST_MARK = 's_vm_fast'

FLAG = r'''static const bool s_rwatch_enabled = getenv("PPU_RWATCH") != nullptr;
/* Fast guest-access paths (Android build tweak). Every accessor below carries diagnostics that are selected by
 * environment variables (function-local statics, each one a guard-variable load on every call). When none of them is
 * set -- the normal case -- the accessors take a short path with only the semantic work: bounds, null page, raw SPU
 * registers, the GCM ref register, the spin-poll detector, store invalidation and coherence. Disabled on Windows and by
 * PPU_VM_SLOW=1. False until initialised, i.e. the slow path. */
static const bool s_vm_fast = []() -> bool {
#ifdef _WIN32
    return false;
#else
    static const char* const diag[] = {"PPU_RWATCH", "YDKJ_RWATCH", "PPU_RVAL", "PPU_HOTMAP", "PPU_SPINBT",
        "PPU_FORCE_READ_ADDR", "PPU_WVAL", "PPU_WWATCH", "PT", "PPU_VM_SLOW", "PS3RECOMP_TRACE_HOTREAD", "PS3_VERBOSE"};
    for (const char* name : diag) if (getenv(name)) return false;
#ifdef VM_SAMPLE_READS
    return false;
#else
    return true;
#endif
#endif
}();
'''

EDITS = [
    ('static const bool s_rwatch_enabled = getenv("PPU_RWATCH") != nullptr;\n', FLAG),
    ('uint32_t vm_read32(uint64_t a) { if (vm_oob((uint32_t)a,4)) return 0;',
     '''uint32_t vm_read32(uint64_t a) {
    if (__builtin_expect(s_vm_fast, 1)) {
        const uint32_t ea = (uint32_t)a;
        if (__builtin_expect(ea >= 0x10000u && ea != (VM_HLE_INJECT_BASE + 0x2008u) && !spu_raw_is_reg(ea), 1) &&
            !vm_oob(ea, 4)) {
            uint32_t v; memcpy(&v, vm_base + ea, 4);
            ppu_poll_read(ea, &v, 4);
            return __builtin_bswap32(v);
        }
    }
    if (vm_oob((uint32_t)a,4)) return 0;'''),
    ('uint64_t vm_read64(uint64_t a) { if (vm_oob((uint32_t)a,8)) return 0;',
     '''uint64_t vm_read64(uint64_t a) {
    if (__builtin_expect(s_vm_fast, 1)) {
        const uint32_t ea = (uint32_t)a;
        if (__builtin_expect(ea >= 0x10000u, 1) && !vm_oob(ea, 8)) {
            uint64_t v; memcpy(&v, vm_base + ea, 8);
            ppu_poll_read(ea, &v, 8);
            return __builtin_bswap64(v);
        }
    }
    if (vm_oob((uint32_t)a,8)) return 0;'''),
    ('void vm_write32(uint64_t a, uint32_t v) { barrier_watch_hit(',
     '''void vm_write32(uint64_t a, uint32_t v) {
    if (__builtin_expect(s_vm_fast && !g_barrier_sync_watch && g_null_sweep_hi == 0, 1)) {
        const uint32_t ea = (uint32_t)a;
        if (__builtin_expect(ea >= 0x1000u, 1) && !vm_oob(ea, 4)) {
            uint32_t bv = __builtin_bswap32(v); VM_WRITE_COH(ea, &bv, 4);
            if (spu_raw_is_reg(ea)) spu_raw_reg_store(ea, v, 4);
            return;
        }
    }
    barrier_watch_hit('''),
    ('void vm_write64(uint64_t a, uint64_t v) {\n',
     '''void vm_write64(uint64_t a, uint64_t v) {
    if (__builtin_expect(s_vm_fast && !g_barrier_sync_watch && g_null_sweep_hi == 0, 1)) {
        const uint32_t ea = (uint32_t)a;
        if (__builtin_expect(ea >= 0x1000u, 1) && !vm_oob(ea, 8)) {
            uint64_t bv = __builtin_bswap64(v); VM_WRITE_COH(ea, &bv, 8);
            return;
        }
    }
'''),
]


def apply_loader_tweaks(path):
    """Apply the edits to ppu_loader.cpp in place. All-or-nothing; returns a short status string."""
    text = path.read_text(encoding='utf-8', errors='surrogateescape')
    if FAST_MARK in text:
        return 'ja aplicado'
    for old, _ in EDITS:
        if text.count(old) != 1:
            return 'ignorado (trecho nao encontrado: ' + old.strip()[:50] + ')'
    for old, new in EDITS:
        text = text.replace(old, new, 1)
    path.write_text(text, encoding='utf-8', errors='surrogateescape')
    return 'aplicado'


def apply(destination):
    """Tweak the private SDK copy made by build_android_core.prepare_sdk."""
    loader = Path(destination) / 'runtime/ppu/ppu_loader.cpp'
    status = apply_loader_tweaks(loader) if loader.is_file() else 'ignorado (ppu_loader.cpp ausente)'
    print('Ajustes Android do SDK (acesso rapido a memoria do jogo): ' + status, flush=True)
