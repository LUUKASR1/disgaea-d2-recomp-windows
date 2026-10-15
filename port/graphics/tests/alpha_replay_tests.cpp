#include "alpha_replay.h"
#include "draw_capture.h"
#include "../../vulkan/draw_packet.h"
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <vector>
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <cmath>
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D12 setup failed");}
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
struct Binding{ID3D12PipelineState* pso;ID3D12RootSignature* root;ID3D12Resource* vb;bool alpha=true,draw=true;};
void record(void* lp,void* bp){
    auto list=static_cast<ID3D12GraphicsCommandList*>(lp);auto b=static_cast<Binding*>(bp);
    list->SetPipelineState(b->pso);list->SetGraphicsRootSignature(b->root);
    float params[4]={b->alpha?1.f:0.f,0,5,0};list->SetGraphicsRoot32BitConstants(0,4,params,0);
    D3D12_VERTEX_BUFFER_VIEW vb{b->vb->GetGPUVirtualAddress(),6*4*sizeof(float),4*sizeof(float)};
    list->IASetVertexBuffers(0,1,&vb);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if(b->draw)list->DrawInstanced(6,1,0,0);
}
int main(){try{
    Ref<IDXGIFactory4> factory;Ref<IDXGIAdapter> warp;Ref<ID3D12Device> device;Ref<ID3D12CommandQueue> queue;
    check(CreateDXGIFactory1(IID_IDXGIFactory4,(void**)factory.put()));check(factory->EnumWarpAdapter(IID_IDXGIAdapter,(void**)warp.put()));
    check(D3D12CreateDevice(warp.p,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)device.put()));
    D3D12_COMMAND_QUEUE_DESC q{};check(device->CreateCommandQueue(&q,IID_ID3D12CommandQueue,(void**)queue.put()));
    const char shader[]=R"(
cbuffer Alpha:register(b1){float4 test;}
struct V {float4 position:SV_Position;float2 uv:TEXCOORD0;};
V vs(float4 p:POSITION){V o;o.position=p;o.uv=p.xy*.5+.5;return o;}
float4 ps(V v):SV_Target {float alpha=(v.uv.x>=.25 && v.uv.x<.5)||(v.uv.x>=.75)?1:0;
    if(test.x!=0 && test.z==5 && alpha<=test.y)discard;
    return float4(0,0,0,0);}
)";
    Ref<ID3DBlob> vs,ps,blob,error;
    check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,vs.put(),error.put()));
    check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,ps.put(),error.put()));
    D3D12_ROOT_PARAMETER param{};param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister=1;param.Constants.Num32BitValues=4;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&param;rd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,blob.put(),error.put()));
    Ref<ID3D12RootSignature> root;check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,(void**)root.put()));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.p;
    pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    D3D12_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
    pd.InputLayout={&element,1};pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;pd.NumRenderTargets=1;
    pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=pd.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
    blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pd.DepthStencilState.FrontFace.StencilFailOp=pd.DepthStencilState.FrontFace.StencilDepthFailOp=pd.DepthStencilState.FrontFace.StencilPassOp=D3D12_STENCIL_OP_KEEP;
    pd.DepthStencilState.FrontFace.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;pd.DepthStencilState.BackFace=pd.DepthStencilState.FrontFace;
    Ref<ID3D12PipelineState> pso;check(device->CreateGraphicsPipelineState(&pd,IID_ID3D12PipelineState,(void**)pso.put()));
    Ref<ID3D12Resource> vb;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=6*4*sizeof(float);
    buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)vb.put()));
    float vertices[6][4]={{-1,-1,.4f,1},{-1,1,.4f,1},{1,1,.4f,1},{-1,-1,.4f,1},{1,1,.4f,1},{1,-1,.4f,1}};
    void* mapped=nullptr;D3D12_RANGE none{0,0};check(vb->Map(0,&none,&mapped));std::memcpy(mapped,vertices,sizeof(vertices));vb->Unmap(0,nullptr);
    Binding binding{pso.p,root.p,vb.p};void* borrowed[]={pso.p,root.p,vb.p};std::vector<unsigned char> mask(32*16);std::vector<float> depth(32*16);
    auto replay=[&]{require(d2_alpha_replay_depth(device.p,queue.p,32,16,record,&binding,borrowed,3,mask.data(),depth.data())==0,"Alpha replay succeeds");};
    replay();for(unsigned y=0;y<16;y++)for(unsigned x=0;x<32;x++)require((mask[y*32+x]!=0)==((x>=8&&x<16)||x>=24),"Discard leaves only surviving quarters despite all-black shader color");
    for(size_t i=0;i<mask.size();i++)require(mask[i]?std::abs(depth[i]-.4f)<1e-6f:depth[i]==1,"Original shader D24 depth agrees with survivor mask; discarded pixels retain clear depth");
    binding.alpha=false;replay();for(auto p:mask)require(p==255,"Disabling alpha test retains full quad");
    binding.draw=false;replay();for(size_t i=0;i<mask.size();i++)require(mask[i]==0 && depth[i]==1,"No draw does not fabricate coverage or depth");
    require(d2_alpha_replay(device.p,queue.p,0,16,record,&binding,borrowed,3,mask.data())<0,"Invalid size rejected");
    SetEnvironmentVariableA("D2_DRAW_CAPTURE","1");
    const char captured_vs[]="float4 main(float4 p:POSITION):SV_Position{return p;}";
    const char captured_fp[]="cbuffer Alpha:register(b1){float4 test;}float4 main():SV_Target{return float4(test.x,0,1,1);}";
    Ref<ID3DBlob> cv,cp;
    check(D3DCompile(captured_vs,sizeof(captured_vs)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,cv.put(),error.put()));
    check(D3DCompile(captured_fp,sizeof(captured_fp)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,cp.put(),error.put()));
    pd.VS={cv->GetBufferPointer(),cv->GetBufferSize()};pd.PS={cp->GetBufferPointer(),cp->GetBufferSize()};
    Ref<ID3D12PipelineState> captured;check(device->CreateGraphicsPipelineState(&pd,IID_ID3D12PipelineState,(void**)captured.put()));
    d2_draw_vertex_source(pd.VS.pShaderBytecode,pd.VS.BytecodeLength,captured_vs);d2_draw_pipeline(captured.p,&pd,captured_fp);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d2_draw_frame_begin();for(unsigned i=0;i<4;i++)d2_draw_srv(i,nullptr,&srv);
    // Odd-width R8/RG8 resources verify GPU copy footprints and component
    // remapping, rather than merely serializing null descriptors.
    Ref<ID3D12Resource> textures[2],uploads[2];Ref<ID3D12CommandAllocator> texture_allocator;Ref<ID3D12GraphicsCommandList> texture_list;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)texture_allocator.put()));
    check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,texture_allocator.p,nullptr,IID_ID3D12GraphicsCommandList,(void**)texture_list.put()));
    for(unsigned unit=0;unit<2;unit++){
        D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=3;td.Height=2;td.DepthOrArraySize=td.MipLevels=1;td.SampleDesc.Count=1;
        td.Format=unit?DXGI_FORMAT_R8G8_UNORM:DXGI_FORMAT_R8_UNORM;D3D12_HEAP_PROPERTIES th{};th.Type=D3D12_HEAP_TYPE_DEFAULT;
        check(device->CreateCommittedResource(&th,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)textures[unit].put()));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;device->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&total);
        auto upload_desc=buffer;upload_desc.Width=total;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&upload_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)uploads[unit].put()));
        void* pixels=nullptr;check(uploads[unit]->Map(0,&none,&pixels));std::memset(pixels,0,(size_t)total);
        for(unsigned y=0;y<2;y++)for(unsigned x=0;x<3;x++)for(unsigned c=0;c<=unit;c++)
            ((unsigned char*)pixels)[footprint.Offset+y*footprint.Footprint.RowPitch+x*(unit+1)+c]=(unsigned char)(17+29*(y*3+x)+c*7);
        uploads[unit]->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=uploads[unit].p;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
        to.pResource=textures[unit].p;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;texture_list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={textures[unit].p,0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};texture_list->ResourceBarrier(1,&barrier);
        auto view=srv;view.Format=td.Format;view.Shader4ComponentMapping=unit?D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0,1,5,4):D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0,0,0,0);
        d2_draw_srv(unit,textures[unit].p,&view);
    }
    check(texture_list->Close());ID3D12CommandList* texture_commands[]={texture_list.p};queue->ExecuteCommandLists(1,texture_commands);
    binding.pso=captured.p;binding.alpha=true;binding.draw=true;
    float expanded[6][64]{};for(unsigned i=0;i<6;i++)std::memcpy(expanded[i],vertices[i],16);
    float vertex_constants[514][4]{};float fragment_constants[4]={1,0,5,0};
    auto folder=std::filesystem::temp_directory_path()/("D2-draw-test-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    auto path=folder.u8string();std::string utf8((char*)path.data(),path.size());
    void* draw_borrowed[]={captured.p,root.p,vb.p};
    require(d2_draw_export(device.p,queue.p,captured.p,record,&binding,utf8.c_str(),42,7,32,16,expanded,6,
        vertex_constants,fragment_constants,sizeof(fragment_constants),0,0,draw_borrowed,3)==0,"Real draw export helper succeeds");
    std::ifstream image(folder/"reference.rgba",std::ios::binary);std::vector<unsigned char> rgba((std::istreambuf_iterator<char>(image)),{});
    require(rgba.size()==32*16*4,"Reference readback strips pitch padding");
    for(size_t i=0;i<rgba.size();i+=4)require(rgba[i]==255&&rgba[i+1]==0&&rgba[i+2]==255&&rgba[i+3]==255,"Original PSO renders magenta reference");
    std::ifstream packet_file(folder/"packet.bin",std::ios::binary);d2::DrawPacket packet;packet_file.read((char*)&packet,sizeof(packet));
    require(bool(packet_file)&&packet.magic==0x31524444&&packet.vertices==6&&packet.fragment_bytes==16,"Portable packet header preserves sizes");
    require(std::filesystem::file_size(folder/"vertices.bin")==sizeof(expanded),"Full 16-attribute geometry exported");
    for(unsigned u=2;u<4;u++)require(std::filesystem::file_size(folder/("tex"+std::to_string(u)+".rgba"))==4,"Null SRV exports one zero texel");
    for(unsigned u=0;u<2;u++){
        std::ifstream tex(folder/("tex"+std::to_string(u)+".rgba"),std::ios::binary);std::vector<unsigned char> pixels((std::istreambuf_iterator<char>(tex)),{});
        require(pixels.size()==24,"Texture footprint stripped to six RGBA texels");
        for(unsigned i=0;i<6;i++){unsigned char value=(unsigned char)(17+29*i);
            require(pixels[i*4]==value&&pixels[i*4+1]==(u?value+7:value)&&pixels[i*4+2]==(u?255:value)&&pixels[i*4+3]==(u?0:value),"GPU texture bytes and SRV remap preserved");}
    }
    d2_draw_frame_begin();require(d2_draw_export(device.p,queue.p,captured.p,record,&binding,utf8.c_str(),42,7,32,16,expanded,6,
        vertex_constants,fragment_constants,sizeof(fragment_constants),0,0,draw_borrowed,3)<0,"Missing descriptor is rejected rather than substituted");
    image.close();packet_file.close();
    d2_draw_capture_close();std::filesystem::remove_all(folder);
    puts("PASS original shader alpha/discard depth mask: zero color, uniform toggle, empty draw");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
