#!/usr/bin/env python3
"""Generate filesystem adapters from the pinned SDK without editing its checkout.

Exact input hashes deliberately reject drift: a changed SDK needs a new review,
not an approximate text replacement. Relative private headers become absolute
so the adapted source can live entirely in the build directory.
"""
import hashlib
from pathlib import Path
import re
import sys

INPUTS = {
    'runtime/ppu/ppu_fs.cpp': '15831bbe39ddba3d7bb7f4c94816b6b6560479b4b9cff192a0ceae0dc8ba11c1',
    'runtime/syscalls/sys_fs.c': '726c6eb43026b7f29b9e507f8833c9ac7f6ff0ebd0cdce1d64b4f19f62cba847',
    'libs/filesystem/cellFs.c': 'fe046ed5ad92b893d31c1713e12a6f1ebe28bbfc62eda1bd9013d87e464e15b9',
}
OPERATIONS = {
    'Open': 'OPEN', 'Close': 'CLOSE', 'Read': 'READ', 'Write': 'WRITE',
    'Lseek': 'SEEK', 'Stat': 'STAT', 'Fstat': 'FSTAT', 'Opendir': 'OPENDIR',
    'Readdir': 'READDIR', 'Closedir': 'CLOSEDIR', 'Mkdir': 'MKDIR',
    'Rmdir': 'RMDIR', 'Unlink': 'UNLINK', 'Fsync': 'FSYNC',
    'FGetBlockSize': 'BLOCK_SIZE', 'GetBlockSize': 'PATH_BLOCK_SIZE',
    'SdataOpen': 'OPEN', 'AllocateFileAreaWithoutZeroFill': 'TRUNCATE',
}


def once(text, pattern, replacement):
    result, count = re.subn(pattern, replacement, text)
    if count != 1:
        raise RuntimeError(f'Expected exactly one SDK anchor, found {count}: {pattern}')
    return result


def ctx_hook(operation, raw=False):
    returned = 'return d2_result;' if raw else 'ctx->gpr[3] = (uint64_t)d2_result; return;'
    return ('\n    { const uint64_t d2_args[6] = {ctx->gpr[3], ctx->gpr[4], ctx->gpr[5], '
            'ctx->gpr[6], ctx->gpr[7], ctx->gpr[8]};\n'
            '      int64_t d2_result = 0;\n'
            f'      if (d2_iso_request(D2_ISO_{operation}, d2_args, &d2_result)) '
            '{ ' + returned + ' } }\n')


def adapt(source, text):
    if source.name == 'ppu_fs.cpp':
        for name, operation in OPERATIONS.items():
            pattern = rf'(static void cellFs{name}\(ppu_context\* ctx\)\s*\{{)'
            text = once(text, pattern, lambda m, op=operation: m[0] + ctx_hook(op))
        anchor = 'else if (fd >= 0 && fd < FS_MAX && g_files[fd]) {'
        replacement = '''else if (d2_iso_is_handle((uint32_t)fd)) {
        uint64_t count = 0; int64_t result = 0;
        if (write) { result = (int32_t)0x80010026; }
        else d2_iso_read_at_guest((uint32_t)fd, offset, buf, size, &count, &result);
        n = (size_t)count; err = (int32_t)result;
    } else if (fd >= 0 && fd < FS_MAX && g_files[fd]) {'''
        text = once(text, re.escape(anchor), lambda _: replacement)
    elif source.name == 'sys_fs.c':
        raw = {'open': 'OPEN', 'read': 'READ', 'write': 'WRITE', 'close': 'CLOSE',
               'lseek': 'SEEK', 'stat': 'STAT', 'fstat': 'FSTAT', 'opendir': 'OPENDIR',
               'readdir': 'READDIR', 'closedir': 'CLOSEDIR', 'mkdir': 'MKDIR',
               'rename': 'RENAME', 'unlink': 'UNLINK', 'rmdir': 'RMDIR', 'ftruncate': 'TRUNCATE'}
        for name, operation in raw.items():
            pattern = rf'(int64_t sys_fs_{name}\(ppu_context\* ctx\)\s*\{{)'
            text = once(text, pattern, lambda m, op=operation: m[0] + ctx_hook(op, raw=True))
    else:
        functions = {
            'Open': ('OPEN', ['path', 'flags', 'fd', 'arg', 'size']),
            'Close': ('CLOSE', ['fd']),
            'Read': ('READ', ['fd', 'buf', 'nbytes', 'nread']),
            'Write': ('WRITE', ['fd', 'buf', 'nbytes', 'nwrite']),
            'Lseek': ('SEEK', ['fd', 'offset', 'whence', 'pos']),
            'Stat': ('STAT', ['path', 'sb']), 'Fstat': ('FSTAT', ['fd', 'sb']),
            'Opendir': ('OPENDIR', ['path', 'fd']),
            'Readdir': ('READDIR', ['fd', 'entry', 'nread']),
            'Closedir': ('CLOSEDIR', ['fd']),
            'Ftruncate': ('TRUNCATE', ['fd', 'size']),
            'Truncate': ('UNLINK', ['path']), 'Chmod': ('UNLINK', ['path']),
            'GetBlockSize': ('PATH_BLOCK_SIZE', ['path', 'sector_size', 'block_size']),
            'Mkdir': ('MKDIR', ['path', 'mode']), 'Rename': ('RENAME', ['from', 'to']),
            'Unlink': ('UNLINK', ['path']),
        }
        for name, (operation, args) in functions.items():
            values = ', '.join(f'(uint64_t)(uintptr_t)({arg})' for arg in args)
            hook = (f'\n    {{ const uint64_t d2_args[6] = {{{values}}}; int64_t d2_result = 0;\n'
                    f'      if (d2_iso_request(D2_ISO_{operation}, d2_args, &d2_result)) return (s32)d2_result; }}\n')
            text = once(text, rf'(s32 cellFs{name}\([^\n]*\)\s*\{{)', lambda m, h=hook: m[0] + h)

    def include(m):
        target = source.parent / m[1]
        return f'#include "{target.resolve().as_posix()}"' if target.is_file() else m[0]
    text = re.sub(r'#\s*include\s*"([^"\n]+)"', include, text)
    return '#include "iso_runtime.h"\n' + text


def generate(sdk, output):
    sdk = sdk.resolve()
    texts = {}
    for relative, digest in INPUTS.items():
        source = sdk / relative
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError('Pinned filesystem SDK source changed: ' + relative)
        texts[source.name] = adapt(source, data.decode('utf-8'))
    output.mkdir(parents=True, exist_ok=True)
    for name, text in texts.items():
        destination = output / name
        if not destination.exists() or destination.read_text(encoding='utf-8') != text:
            destination.write_text(text, encoding='utf-8', newline='\n')
    print('Generated three ISO filesystem adapters; pinned SDK preserved.')


if __name__ == '__main__':
    generate(Path(sys.argv[1]), Path(sys.argv[2]))
