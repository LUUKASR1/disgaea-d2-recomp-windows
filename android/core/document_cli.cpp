#include "document_probe.h"
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <string>
int main(int argc,char **argv) {
    if(argc!=2 && argc!=4) return 2;
    int fd=open(argv[1],O_RDONLY|O_CLOEXEC);
    if(fd<0) return 2;
    try {
        auto report=d2_inspect_document(fd,argc==4 ? std::stoll(argv[2]) : 0,
                                       argc==4 ? std::stoll(argv[3]) : -1);
        close(fd); std::cout<<report; return 0;
    } catch(...) { close(fd); return 2; }
}
