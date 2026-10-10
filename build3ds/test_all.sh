#!/usr/bin/env bash
# Run every built gate in Azahar, one at a time, and print a pass/fail table.
cd "$(dirname "$0")"
pass=0; total=0
missing=0
for g in smoke smoke_heap smoke_roots smoke_fs smoke_gx smoke_model smoke_anim smoke_frames smoke_soak smoke_soak_anim smoke_modelanim smoke_oam smoke_clsn smoke_actor smoke_softfloat smoke_fader smoke_particle_math smoke_clock smoke_alarm_queue smoke_alarm_dispatch smoke_player; do
  [ -f cmake/$g.3dsx ] || { printf "%-16s NOT BUILT\n" $g; missing=$((missing+1)); continue; }
  total=$((total+1))
  r=$(./run.sh cmake/$g.3dsx ${TIMEOUT:-180} | sed -n 's/^RESULT: //p')
  [ "$r" = PASS ] && pass=$((pass+1))
  printf "%-16s %s\n" $g "$r"
done
echo "== $pass / $total built gates pass on 3DS (Azahar)"

[ "$pass" -eq "$total" ] && [ "$missing" -eq 0 ]
