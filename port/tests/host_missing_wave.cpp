// Retail ARM7 note rejection: missing waves do not abort the sequencer.
#include "third_party/sseqplayer/Player.h"
#include <cassert>
#include <cstdio>
int main(){
 unsigned cases=0;
 for(unsigned slot=0;slot<4;++slot)for(unsigned absent=0;absent<2;++absent){
  SBNK bank;bank.instruments.resize(1);
  auto& instrument=bank.instruments[0];instrument.record=1;
  instrument.ranges.emplace_back(0,127,1);auto& range=instrument.ranges[0];
  range.swar=slot;range.swav=2;range.noteNumber=60;
  range.attackRate=range.decayRate=range.sustainLevel=range.releaseRate=127;range.pan=64;
  SWAR empty; if(!absent)bank.waveArc[slot]=&empty;
  SSEQ seq;seq.bank=&bank;seq.data={0xff};
  Player player;assert(player.Setup(&seq));auto& track=player.tracks[player.trackIds[0]];
  assert(track.NoteOn(71,127,12)==-1);
  for(const auto& channel:player.channels)assert(channel.state==CS_NONE);
  // A subsequent valid PSG note on the same track must still work.
  instrument.record=2;range.record=2;range.swav=2;
  const int channel=track.NoteOn(60,127,12);assert(channel>=0);
  assert(player.channels[channel].state==CS_START);++cases;
 }
 std::printf("PASS missing wave note rejection and subsequent valid note: %u cases\n",cases);
}
