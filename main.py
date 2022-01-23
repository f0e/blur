import argparse
import sys
import os
import subprocess
sys.path.insert(0, 'scripts')
from config import ConfigExist
from render import Render

# CWD
CWD=os.environ["APPDATA"]+"/Smoothie"
if os.path.exists(CWD) is False:
    os.mkdir(CWD)
    
ConfigExist()

Parser=argparse.ArgumentParser(prog="Smoothie",
usage="""
smoothie -blur Video1.mp4 Video2.mp4 Video3.mp4
smoothie -frameblend Video1.mp4 Video2.mp4 Video3.mp4""")

Parser.add_argument('-blur',action="store",
help="Blur single or multiple videos at once.",
metavar=("<Videos>"))

Parser.add_argument('-frameblend',action="store",
help="Interpolate and blur single or multiple videos at once.",
metavar=("<Videos>"))

Parser.add_argument('-edit',action="store_true",help="Edit Smoothie's config file.")

Arguments=Parser.parse_args()

if Arguments.blur is not None:
    Render(Arguments.blur, "blur")
elif Arguments.frameblend is not None:
    Render(Arguments.frameblend, "frameblend")
elif Arguments.edit is True:
    subprocess.Popen(['start',f'{CWD}/Smoothie-Config.ini'],shell=True)     
