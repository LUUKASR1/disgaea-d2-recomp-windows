#include "document_probe.h"
#include "descriptor_file.h"
#include "iso_vfs.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>

std::string d2_inspect_document(int fd,int64_t base,int64_t declared_length) {
    std::string report="document_reader=bounded-descriptor-slice\n"
        "document_game_boot=false\ndocument_ps3_mount_available=false\n";
    auto fail=[&](const char *reason) { return report+"document_status=rejected\ndocument_reason="+reason+"\n"; };
    if(base<0 || declared_length< -1) return fail("invalid-slice");
    struct stat st{};
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0) return fail("regular-seekable-file-required");
    uint64_t offset=static_cast<uint64_t>(base), size=static_cast<uint64_t>(st.st_size);
    if(offset>size) return fail("slice-outside-file");
    uint64_t length=declared_length<0 ? size-offset : static_cast<uint64_t>(declared_length);
    d2::DescriptorFile view;
    if(!view.attach(fd,offset,length)) return fail("unreadable-or-out-of-bounds-slice");
    std::array<unsigned char,64*1024> head{};
    size_t count=length<head.size() ? static_cast<size_t>(length) : head.size();
    if(!view.read(0,head.data(),count)) return fail("sample-read-failed");
    std::array<unsigned char,64> tail{};
    size_t tail_count=length<tail.size() ? static_cast<size_t>(length) : tail.size();
    if(!view.read(length-tail_count,tail.data(),tail_count)) return fail("tail-read-failed");
    unsigned char sentinel=0xa5;
    if(view.read(length,&sentinel,1) || sentinel!=0xa5 || !view.read(length,nullptr,0))
        return fail("boundary-check-failed");
    const char *hint="unknown";
    if(count>=4 && !std::memcmp(head.data(),"\x7f" "ELF",4)) hint="elf-header-only";
    else if(count>=4 && !std::memcmp(head.data(),"SCE\0",4)) hint="self-header-only";
    else if(count>=4 && !std::memcmp(head.data(),"\0PSF",4)) hint="sfo-header-only";
    else if(count>=32774 && !std::memcmp(head.data()+32769,"CD001",5)) hint="iso9660-header-only";
    else {
        for(size_t sector=16;sector<32 && sector*2048+6<=count;++sector)
            if(!std::memcmp(head.data()+sector*2048+1,"NSR02",5) ||
               !std::memcmp(head.data()+sector*2048+1,"NSR03",5)) hint="udf-header-only";
    }
    report+="document_bytes="+std::to_string(length)+"\ndocument_head_sample_bytes="+std::to_string(count)+
        "\ndocument_tail_sample_bytes="+std::to_string(tail_count)+"\ndocument_container_hint="+hint+
        "\ndocument_boundary_check=passed\ndocument_status="+(length ? std::string("readable") : std::string("empty"))+"\n";
    if(std::strcmp(hint,"iso9660-header-only") && std::strcmp(hint,"udf-header-only"))
        return report+"disc_status=not-disc\n";
    report+="disc_mount_scope=read-only-inspection\ndisc_game_boot=false\n";
    try {
        d2::vfs::IsoVfs disc(fd,offset,length);
        report+="disc_backend="+disc.BackendName()+"\n";
        auto title=disc.TitleId();
        report+="disc_title_id="+title+"\n";
        if(title!="BLUS31313") return report+"disc_status=other-title\n";
        unsigned checked=0, failed=0;
        auto check=[&](const std::string &path) {
            try {
                auto file=disc.Open(path);
                std::array<std::byte,4096> sample{};
                if(!file->Size() || file->ReadAt(0,sample)!=std::min<std::uint64_t>(file->Size(),sample.size())) ++failed;
                else ++checked;
            } catch(const d2::vfs::Error&) { ++failed; }
        };
        check("/PS3_GAME/PARAM.SFO"); check("/PS3_GAME/USRDIR/EBOOT.BIN");
        for(const char *stem:{"START","ANM_HI"}) for(unsigned part=0;part<7;++part)
            check(std::string("/PS3_GAME/USRDIR/Data/")+stem+(part ? "_"+std::to_string(part) : "")+".dat");
        check("/PS3_GAME/USRDIR/Data/NisAnmShadow.txf");
        check("/PS3_GAME/USRDIR/Data/SOUND/SndPakBgm/SndPakBgm_0.pak");
        report+="disc_required_files_checked="+std::to_string(checked)+"\ndisc_required_files_failed="+
            std::to_string(failed)+"\ndisc_asset_decryption_validated=false\ndisc_status="+
            (failed ? std::string("missing-or-unreadable-base-files") : std::string("base-files-readable"))+"\n";
    } catch(const d2::vfs::Error &e) {
        // Do not include provider URI, host paths or untrusted on-disc strings.
        report+="disc_status=rejected\ndisc_error_code="+std::to_string(static_cast<int>(e.code))+"\n";
    }
    return report;
}
