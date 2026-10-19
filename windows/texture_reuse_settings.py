"""Explicit per-launch experiment; not a saved graphics setting or stable feature."""
def texture_reuse_environment(original, enabled=False, official=False):
    env = {k: v for k, v in original.items() if k.upper() != 'D2_TEXTURE_REUSE'}
    env['D2_TEXTURE_REUSE'] = '1' if enabled and not official else '0'
    return env


def vertex_context_environment(original, enabled=False, official=False):
    env = {k: v for k, v in original.items() if k.upper() != 'D2_VERTEX_CONTEXT'}
    env['D2_VERTEX_CONTEXT'] = '1' if enabled and not official else '0'
    return env
