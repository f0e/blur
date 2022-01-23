import os
from config import ConfigRead 
from vs_script import Blur
from vs_script import Interpolate
from vs_script import Frameblend
import subprocess
Temp=os.environ["TEMP"]

def Render(VideoList, Option):
    Settings=ConfigRead(Option)
    for Video in [VideoList]:
        Video=os.path.abspath(Video)
        VideoPath=os.path.split(Video)[0]
        VideoFile=os.path.split(Video)[1]

        if Option == "blur":
            Blur(Video,Settings[2],Settings[3])
            Prefix="Blurred"
        elif Option == "interpolate":
            Interpolate(Video,Settings[2],Settings[3],Settings[4],Settings[5])
            Prefix="Interpolated"
        elif Option == "frameblend":
            Frameblend(Video,Settings[2],Settings[3],Settings[4],Settings[5],Settings[6],Settings[7]) 
            Prefix="Frameblended"  
        try:        
            os.remove(VideoPath+f"/{Prefix}_{VideoFile}")
        except:
            pass    
        Main=['vspipe', '-y', Temp+'/Render.vpy','-','|','ffmpeg.exe','-y']+Settings[0].split(" ")+['-loglevel', 
        'error', '-hide_banner', '-stats', '-i', '-']+Settings[1].split(" ")+[VideoPath+f"/{Prefix}_"+VideoFile]
  
            
        print(f"Video: {Video}")      
        subprocess.run(Main,shell=True) 
        print("")

        

