import os
from config import ConfigRead 
from vs_script import Resample
from vs_script import Interpolate
from re import findall
from time import sleep
import resource
import subprocess

Temp=os.environ["TEMP"]

# Variables

def Render(VideoList, Option):
    Settings=ConfigRead(Option)
    for Video in list(VideoList):
        Video=os.path.abspath(Video)
        VideoPath=os.path.split(Video)[0]
        VideoFile=os.path.split(Video)[1]

        if Option == "resample":
            Resample(Video,Settings[2],Settings[3])
            Prefix="Resampled"
        elif Option == "interpolate":
            Interpolate(Video,Settings[2],Settings[3],Settings[4],Settings[5],Settings[6],Settings[7]) 
            Prefix="Interpolated" 
        if os.path.isfile(f"{VideoPath}\\{Prefix}_{VideoFile}"):
            os.remove(f"{VideoPath}\\{Prefix}_{VideoFile}")
        # Variables
        if "ffmpeg.exe" in os.path.split(Settings[0])[1]:
            InFile="-"
            Pipe=['vspipe', '-y', Temp+'/Render.vpy','-','|']
        else:
            InFile=Temp+'/Render.vpy' 
            Pipe=[]
        OutFile=f"{VideoPath}\\{Prefix}_{VideoFile}"
        resource.setrlimit(resource.RLIMIT_CPU)
        Main=Pipe+[Settings[0]]+findall(r'[A-Z]+:(?:\\[^\\]+)+\.\w+|\S+',f"{Settings[1]}".format(Input=InFile,Output=OutFile)) 
        print(f"Video: {Video}\n")      
        subprocess.run(Main,shell=True) 
        print("")

        

