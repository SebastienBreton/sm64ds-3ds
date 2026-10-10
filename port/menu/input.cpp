// Native touch sampling for the menu's optional SDK-input path.
// The menu renders DS coordinates 256x192; fullscreen 3DS bottom is 320x240.
#include <3ds.h>
#include <cstdint>
#include "platform/3ds/ds_input.h"
extern "C" void sm64ds_menu_sample_touch(std::uint16_t* sample) {
 touchPosition position{};
 hidTouchRead(&position);
 sample[0]=position.px<320?position.px*256/320:255;
 sample[1]=position.py<240?position.py*192/240:191;
 sample[2]=(hidKeysHeld()&KEY_TOUCH)?1:0;
 sample[3]=0; // libctru already supplies calibrated coordinates.
}


extern "C" {
int func_0203da9c();
void func_0203e0ac();
extern unsigned char data_020a1040[];
}
// Local mode of the native input dispatcher. Preserve the source record's
// camera heading (+0x10), published by Camera::Behavior on the previous tick.
// The original transfer copies it to every local session record together with
// the buttons/touch data; directly writing destination keys loses this heading.
extern "C" bool sm64ds_local_input_publish(std::uint16_t keys,const std::uint16_t* touch) {
 if(func_0203da9c()!=0)return false; // remote sessions need their native transport
 auto* source=reinterpret_cast<std::uint16_t*>(data_020a1040);
 for(unsigned i=0;i<4;++i)source[2+i]=touch[i];
 // Released pen: the original func_0203b9bc publishes x=y=0xff, pressed=0,
 // invalid=0 (melonDS reference, physics-levels A/B traces).
 if(!touch[2]){source[2]=0xff;source[3]=0xff;source[4]=0;source[5]=0;}
 source[7]=keys&0x0fff;
 source[6]|=0x8000;
 func_0203e0ac();
 ++*reinterpret_cast<std::uint32_t*>(data_020a1040);
 return true;
}
// Caller scans libctru HID once before polling.
extern "C" bool sm64ds_menu_poll_input() {
 std::uint16_t touch[4];sm64ds_menu_sample_touch(touch);
 return sm64ds_local_input_publish(ds_input::buttons(hidKeysHeld()),touch);
}

// Original main-loop input history/reset-combination BSS (020a0e44..58).
extern "C" {
std::uint8_t data_02099e18=1; // ROM 02099e18: one local session.
std::uint8_t data_020a0e44;
std::uint16_t data_020a0e48[4];
std::uint16_t data_020a0e50[4];
}
