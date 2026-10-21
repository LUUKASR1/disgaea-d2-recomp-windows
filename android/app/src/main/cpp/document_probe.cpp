#include "document_probe.h"
#include "descriptor_file.h"
#include <array>
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
    return report;
}
