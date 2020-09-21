//See LICENSE for license details
#ifdef TRACERSBRIDGEMODULE_struct_guard

#include "tracers.h"

#include <stdio.h>
#include <string.h>

tracers_t::tracers_t(
    simif_t *sim,
    TRACERSBRIDGEMODULE_struct * mmio_addrs,
    int tracerno) :
        bridge_driver_t(sim),
        mmio_addrs(mmio_addrs){
    //Biancolin: move into elaboration
}

tracers_t::~tracers_t() {
    free(this->mmio_addrs);
}

void tracers_t::init() {
    printf("tracers init\n");
    write(this->mmio_addrs->traceActive, true);
    write(this->mmio_addrs->initDone, true);
}

void tracers_t::tick() {
    uint32_t li = read(this->mmio_addrs->lastInstruction);
    bool valid = li & 0x1;
    uint32_t addr = li ^ valid;
    if(valid){
        printf("tracers %x\n", addr);
    } else{
        printf("tracers invalid\n");
    }
}


void tracers_t::finish() {
    printf("tracers - li_valid: %x\n", read(this->mmio_addrs->li_valid));
    printf("tracers - li_iaddr: %x\n", read(this->mmio_addrs->li_iaddr));
    printf("tracers - li_insn: %x\n", read(this->mmio_addrs->li_insn));
//    printf("tracers - li_wdata: %x\n", read(this->mmio_addrs->li_wdata));
    printf("tracers - li_priv: %x\n", read(this->mmio_addrs->li_priv));
    printf("tracers - li_exception: %x\n", read(this->mmio_addrs->li_exception));
    printf("tracers - li_interrupt: %x\n", read(this->mmio_addrs->li_interrupt));
    printf("tracers - li_cause: %x\n", read(this->mmio_addrs->li_cause));
    printf("tracers - li_tval: %x\n", read(this->mmio_addrs->li_tval));
}

#endif // TRACERSBRIDGEMODULE_struct_guard
