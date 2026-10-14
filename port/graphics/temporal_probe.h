#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define D2_TEMP_ROWS 514
typedef struct { unsigned char used[D2_TEMP_ROWS]; unsigned count; int indirect; } D2ConstantReads;
static inline D2ConstantReads d2_constant_reads(const char* text){
    D2ConstantReads result={0};if(!text)return result;
    const char* p=text;
    while((p=strstr(p,"cb0["))!=NULL){
        p+=4;char* end=NULL;
        if(*p<'0'||*p>'9'){result.indirect=1;continue;}
        unsigned long row=strtoul(p,&end,10);
        if(end && *end==']' && row<D2_TEMP_ROWS && !result.used[row]){
            result.used[row]=1;result.count++;
        }else if(end && *end!=']')result.indirect=1;
    }
    return result;
}
