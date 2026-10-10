// Native touch sampling for the menu's optional SDK-input path.
// The menu renders DS coordinates 256x192; fullscreen 3DS bottom is 320x240.
#include <3ds.h>
#include <cstdint>
#include <cstdlib>
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
// DS pen record timing (face-toy, melonDS reference): the game's touch record
// reports the pen-down state one update late while the position is current:
//   record(k) = pressed(k-1) ? (pressed(k) ? pos(k) : pos(k-1)) : released.
// A press is seen one update after contact and a release one update late (the
// last position is held); no tap is lost. The title-screen face toy grabs at
// the second sample of a stroke on DS. SM64DS_DS_PEN_LATENCY=0 publishes the
// raw 3DS pen instead. Scripted harnesses publish directly and are unaffected.
static void ds_pen_record(std::uint16_t* touch){
 static const bool enabled=[]{const char* s=std::getenv("SM64DS_DS_PEN_LATENCY");return !(s&&s[0]=='0');}();
 static bool previous_down=false;static std::uint16_t previous_x=0,previous_y=0;
 if(!enabled)return;
 const bool down=touch[2]!=0;const std::uint16_t x=touch[0],y=touch[1];
 if(!previous_down){touch[0]=touch[1]=0;touch[2]=0;}
 else if(!down){touch[0]=previous_x;touch[1]=previous_y;touch[2]=1;}
 previous_down=down;if(down){previous_x=x;previous_y=y;}
}
// Caller scans libctru HID once before polling.
// visual-enhancements: the SELECT+START settings screen filters the keys
// (enhance_runtime.h); while it is open the game sees no buttons and no pen.
namespace enhance_runtime { std::uint32_t filter_input(std::uint32_t held,std::uint32_t down,bool& touch_allowed); }
extern "C" bool sm64ds_menu_poll_input() {
 std::uint16_t touch[4];sm64ds_menu_sample_touch(touch);
 bool touch_allowed=true;
 const std::uint32_t held=enhance_runtime::filter_input(hidKeysHeld(),hidKeysDown(),touch_allowed);
 if(!touch_allowed){touch[0]=touch[1]=0;touch[2]=0;}
 ds_pen_record(touch);
 return sm64ds_local_input_publish(ds_input::buttons(held),touch);
}

// Original main-loop input history/reset-combination BSS (020a0e44..58).
extern "C" {
std::uint8_t data_02099e18=1; // ROM 02099e18: one local session.
std::uint8_t data_020a0e44;
std::uint16_t data_020a0e48[4];
std::uint16_t data_020a0e50[4];
}
