import configparser
import encoder_settings
import os

File=os.environ["APPDATA"]+"/Smoothie/Smoothie-Config.ini"

def ConfigExist():
  if os.path.exists(File) is False:
    ConfigCreate()

def ConfigCreate():
    SmoothieConfig = configparser.ConfigParser()
    EncoderSettings=encoder_settings.Settings()
    # Video
    SmoothieConfig['interpolation'] = {
    'fps': '480',
    'speed': 'medium',
    'tuning': 'weak',
    'algorithm': '23'
    }

    SmoothieConfig['blur'] = {
      'fps': '60', 
      'amount': '1'
      }   

    # Rendering
    SmoothieConfig['rendering'] = {
    "hw accel args": EncoderSettings[0],
    "ffmpeg args": EncoderSettings[1]} 

    # Create Config File
    with open(File, 'w') as configfile:
        SmoothieConfig.write(configfile)

def ConfigRead(Option):
  SmoothieConfig = configparser.ConfigParser()
  SmoothieConfig.read(File)

  HWAccelArgs=SmoothieConfig['rendering']['hw accel args']
  FFmpegArgs=SmoothieConfig['rendering']['ffmpeg args']

  if Option == "blur":
    FPS=SmoothieConfig['blur']['fps']

    Amount=SmoothieConfig['blur']['amount']

    return (HWAccelArgs, FFmpegArgs, Amount, FPS) 
    
  elif Option == "interpolate":
    FPS=SmoothieConfig['interpolation']['fps']

    Speed=SmoothieConfig['interpolation']['speed']

    Tuning=SmoothieConfig['interpolation']['tuning']

    Algorithm=SmoothieConfig['interpolation']['algorithm']

    return (HWAccelArgs, FFmpegArgs, FPS, Speed, Tuning, Algorithm) 

  elif Option == "frameblend":   
    InterpolateFPS=SmoothieConfig['interpolation']['fps']

    Speed=SmoothieConfig['interpolation']['speed']

    Tuning=SmoothieConfig['interpolation']['tuning']

    Algorithm=SmoothieConfig['interpolation']['algorithm']

    BlurFPS=SmoothieConfig['blur']['fps']

    Amount=SmoothieConfig['blur']['amount']

    return (HWAccelArgs, FFmpegArgs, 
    InterpolateFPS, Speed, Tuning, Algorithm, 
    BlurFPS, Amount)   

    



