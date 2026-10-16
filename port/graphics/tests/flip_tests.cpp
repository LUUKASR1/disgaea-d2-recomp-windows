#include "flip_policy.h"
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    require(!d2_flip_eligible(1,0,4,4,1,10,11,9,0,0),"Capped frame must wait for next vblank");
    require(!d2_flip_eligible(1,0,4,4,1,10,10,10,0,0),"Capped frames cannot share vblank");
    require(d2_flip_eligible(1,0,4,4,1,11,11,10,0,0),"Capped next vblank becomes eligible");
    for(unsigned sequence=0;sequence<100;sequence++)
        require(d2_flip_eligible(1,0,sequence,sequence,1,10,11,10,0,1),"Unlimited completed frames may share vblank");
    for(int unlimited:{0,1})for(int vsync:{0,1}){
        require(!d2_flip_eligible(0,0,4,4,vsync,20,11,10,0,unlimited),"No pending flip must never present");
        require(!d2_flip_eligible(1,1,4,4,vsync,20,11,10,0,unlimited),"Incomplete FIFO must never present");
        require(!d2_flip_eligible(1,0,5,4,vsync,20,11,10,0,unlimited),"Unretired ticket must never present twice");
    }
    require(d2_flip_eligible(1,0,4,4,0,10,11,10,0,0),"Guest HSYNC retains its existing behavior");
    require(d2_flip_eligible(1,0,4,4,1,10,11,9,1,0),"Legacy immediate mode still waits for a new vblank");
    puts("PASS flip timing: same-vblank uncapped; FIFO, ticket and capped order preserved");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
