// Included by capture.cpp. Opt-in export of the HLSL that passed D3DCompile.
// No shader, GPU resource, constant or render state is modified here.
namespace {
struct ShaderCorpus {
    bool initialized=false, enabled=false, capped=false;
    unsigned vertex=0, fragment=0, raw_programs=0, errors=0;
    size_t bytes=0;
    std::string folder;
    std::set<std::string> seen;
} corpus;
bool corpus_write(const std::string& name,const void* data,size_t bytes) {
    auto temporary=name+".tmp";
    FILE* file=d2_capture_fopen(temporary.c_str(),"wb");
    if(!file) return false;
    auto written=std::fwrite(data,1,bytes,file);
    int closed=std::fclose(file);
    auto from=wide(temporary.c_str()),to=wide(name.c_str());
    if(written!=bytes || closed!=0 || !MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(from.c_str());return false;
    }
    return true;
}
void corpus_manifest(bool closed) {
    if(corpus.folder.empty()) return;
    char json[768];
    int n=std::snprintf(json,sizeof(json),
        "{\"schema\":1,\"purpose\":\"runtime-hlsl-corpus\",\"status\":\"%s\","
        "\"reference\":\"d3dcompile-success\",\"vertex_shaders\":%u,\"fragment_shaders\":%u,"
        "\"bytes\":%zu,\"raw_programs\":%u,\"errors\":%u,\"capped\":%s,\"file_limit\":256,\"byte_limit\":33554432,"
        "\"normal_rendering_changed\":false,\"all_game_shaders_collected\":false,"
        "\"vulkan_rendering_validated\":false,\"rpcs3_comparison_performed\":false}\n",
        closed?"closed":"collecting",corpus.vertex,corpus.fragment,corpus.bytes,corpus.raw_programs,corpus.errors,corpus.capped?"true":"false");
    if(n>0 && size_t(n)<sizeof(json) && !corpus_write(corpus.folder+"/corpus.json",json,size_t(n))) {
        if(corpus.errors++<4) std::fprintf(stderr,"[D2SHADER] Cannot update corpus manifest\n");
    }
}
void corpus_close() { if(corpus.enabled) corpus_manifest(true); corpus={}; }
}
extern "C" int d2_capture_shader_enabled(void) {
    if(!corpus.initialized) {
        corpus.initialized=true;
        char option[4]={};
        corpus.enabled=GetEnvironmentVariableA("D2_SHADER_CORPUS",option,sizeof(option))==1 && option[0]=='1';
        if(corpus.enabled) {
            const char* base=d2_capture_directory();
            if(base) {corpus.folder=std::string(base)+"/shaders";d2_capture_mkdir(corpus.folder.c_str());}
        }
    }
    return corpus.enabled && !corpus.folder.empty();
}
extern "C" void d2_capture_shader_program(const char* stage,const char* hlsl,size_t bytes, const void* raw,size_t raw_bytes,uint32_t start_slot,uint32_t control,uint32_t cube_mask) {
    if(!d2_capture_shader_enabled()) return;
    if(!corpus.enabled || corpus.folder.empty() || !stage || !hlsl || !bytes || bytes>262144 ||
       (std::strcmp(stage,"vp")!=0 && std::strcmp(stage,"fp")!=0) || raw_bytes>65536 || (raw_bytes && !raw)) return;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    unsigned char hash[32]={};
    NTSTATUS status=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0);
    if(status>=0) status=BCryptHash(algorithm,nullptr,0,(PUCHAR)hlsl,ULONG(bytes),hash,sizeof(hash));
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0) {corpus.errors++;return;}
    char hex[65];for(unsigned i=0;i<32;i++) std::snprintf(hex+2*i,3,"%02x",hash[i]);
    std::string key=std::string(stage)+"_"+hex;
    if(corpus.seen.count(key)) return;
    char filename[32];std::snprintf(filename,sizeof(filename),"%s_%03u.hlsl",stage,unsigned(corpus.seen.size()));
    char metadata[512];
    int metadata_bytes=std::snprintf(metadata,sizeof(metadata),
        "{\"schema\":1,\"stage\":\"%s\",\"sample\":\"first-compiled-instance\",\"program_start_slot\":%u,\"shader_control\":%u,\"cube_mask\":%u,\"raw_bytes\":%zu,\"sha256_hlsl\":\"%s\",\"reference_compile_succeeded\":true}\n",
        stage,start_slot,control,cube_mask,raw_bytes,hex);
    if(metadata_bytes<=0 || size_t(metadata_bytes)>=sizeof(metadata)) return;
    size_t payload=bytes+raw_bytes+size_t(metadata_bytes);
    if(corpus.seen.size()>=256 || payload>33554432-corpus.bytes) {
        if(!corpus.capped) {corpus.capped=true;corpus_manifest(false);}return;
    }
    if(!corpus_write(corpus.folder+"/"+filename,hlsl,bytes)) {
        if(corpus.errors++<4) std::fprintf(stderr,"[D2SHADER] Cannot export %s\n",filename);
        corpus_manifest(false);return;
    }
    bool raw_ok=!raw_bytes || corpus_write(corpus.folder+"/"+filename+".rsx",raw,raw_bytes);
    bool meta_ok=corpus_write(corpus.folder+"/"+filename+".json",metadata,size_t(metadata_bytes));
    if(!raw_ok || !meta_ok) corpus.errors++;
    if(raw_bytes && raw_ok) corpus.raw_programs++;
    corpus.seen.insert(key);corpus.bytes+=payload;
    if(stage[0]=='v') corpus.vertex++;else corpus.fragment++;
    corpus_manifest(false);
}

extern "C" void d2_capture_shader(const char* stage,const char* hlsl,size_t bytes) {
    d2_capture_shader_program(stage,hlsl,bytes,nullptr,0,0,0,0);
}
