#pragma once
#include <cstdint>
// ARM7 image0x3450, then the seven volume bits accepted by the sound register.
inline unsigned sm64ds_surround_volume(unsigned volume,unsigned pan,unsigned amount){
 if(pan>=24 && pan<=104)return volume;
 const std::uint32_t factor=pan<24?
   amount*(pan+40)+(0x7fffu-amount)*64:
   (amount+0x7fffu)*64-amount*(pan-40);
 return unsigned(static_cast<std::int32_t>(volume*factor)>>21)&127;
}
