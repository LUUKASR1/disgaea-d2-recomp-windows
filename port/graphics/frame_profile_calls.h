#pragma once
/* Included after the D3D headers. Preserve arguments, return values and ownership.
 * These wall times are subsets of preparation, not additional GPU/CPU times. */
static HRESULT d2_profile_create_resource(ID3D12Device* device,
    const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags,
    const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state,
    const D3D12_CLEAR_VALUE* clear, REFIID iid, void** result)
{
    int enabled = d2_lite_enabled();
    double start = enabled ? d2_lite_now() : 0;
    HRESULT hr = device->lpVtbl->CreateCommittedResource(device, heap, flags, desc, state, clear, iid, result);
    if (enabled) {
        d2_profile_resource_ms += (d2_lite_now() - start) * 1000;
        if (desc && desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) d2_profile_buffers++;
        else d2_profile_textures++;
        if (FAILED(hr)) d2_profile_resource_failures++;
    }
    return hr;
}

static HRESULT d2_profile_compile(LPCVOID data, SIZE_T size, LPCSTR source,
    const D3D_SHADER_MACRO* macros, ID3DInclude* include, LPCSTR entry,
    LPCSTR target, UINT flags1, UINT flags2, ID3DBlob** code, ID3DBlob** errors)
{
    int enabled = d2_lite_enabled();
    double start = enabled ? d2_lite_now() : 0;
    HRESULT hr = D3DCompile(data, size, source, macros, include, entry, target, flags1, flags2, code, errors);
    if (enabled) {
        d2_profile_shader_ms += (d2_lite_now() - start) * 1000;
        d2_profile_shader_calls++;
    }
    return hr;
}

static HRESULT d2_profile_create_pso(ID3D12Device* device,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID iid, void** result)
{
    int enabled = d2_lite_enabled();
    double start = enabled ? d2_lite_now() : 0;
    HRESULT hr = device->lpVtbl->CreateGraphicsPipelineState(device, desc, iid, result);
    if (enabled) {
        d2_profile_pso_ms += (d2_lite_now() - start) * 1000;
        d2_profile_pso_calls++;
        if (FAILED(hr)) d2_profile_pso_failures++;
    }
    return hr;
}
