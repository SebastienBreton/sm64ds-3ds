#!/usr/bin/env python3
"""Run one occupied-file touch journey on its own cart, then compare retail writes.
Requires the shared Azahar slot to be reserved by the caller. run.py kills it
between runs and refuses concurrent use through its SD lock.
"""
import argparse,subprocess,sys,shutil,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument("action",choices=["copy-no","copy-yes","erase-no","erase-yes"]);a=p.parse_args()
r=Path(__file__).resolve().parent
out=r/"validation"/("menu-ui-"+a.action)
sd=Path.home()/"Library/Application Support/Azahar/sdmc/sm64ds"
save=sd/("menu-ui-"+a.action+".sav")
shutil.copyfile(r/"reference-menu/diagnostic-occupied.sav",save)
subprocess.run([sys.executable,str(r/"run.py"),str(r/"menu-compile/smoke_menu_frames.3dsx"),"--timeout","100","--env","SM64DS_MENU_TRANSACTION="+a.action,"--env","SM64DS_SAVE_PATH=sdmc:/sm64ds/"+save.name,"--output",str(out)],check=True)
shutil.copyfile(save,out/"cart-after.sav")
assert json.loads((out/"run.json").read_text())["unmapped_access_count"]==0
subprocess.run([sys.executable,str(r/"check_menu_ui_save.py"),a.action,str(out/"cart-after.sav"),str(out/"transaction.json")],check=True)
