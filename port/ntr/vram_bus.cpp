#include "ntr/vram_bus.h"
namespace ntr {
namespace { VramBanks* active_bus = nullptr; }
VramBanks* io_vram_banks(){return active_bus;}
void vram_attach_cpu_bus(VramBanks* bus){active_bus=bus;}
uint32_t vram_cpu_read(uint32_t address,unsigned width){return active_bus?active_bus->read_cpu(address,width):0;}
void vram_cpu_write(uint32_t address,uint32_t value,unsigned width){if(active_bus)active_bus->write_cpu(address,value,width);}
VramSpan vram_cpu_span(uint32_t address){return active_bus?active_bus->unique_cpu_span(address):VramSpan{};}
void vram_physical_changed(unsigned bank,unsigned offset,unsigned bytes){if(active_bus)active_bus->changed(bank,offset,bytes);}
}
