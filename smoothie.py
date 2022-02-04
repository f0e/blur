from sys import argv # parse args
from os import path # split file extension
from configparser import ConfigParser
from subprocess import run # Run vs
from random import choice # Randomize the smoothie's flavor))

# Bool aliases
yes = ['True','true','yes','y','1']
no = ['False','false','no','n','0','null','',None]

if len(argv) == 1:
    print('''
using Smoothie from the command line:

sm "D:\Video\input1.mp4" "D:\Video\input2.mp4" ...
    Simply give in the path of the videos you wish to queue to smoothie

sm config.extension "D:\Video\input1.mp4" "D:\Video\input2.mp4" ...
    You can also make the first argument be your custom config file's name, it'll look for it in the settings folder
    ''')
    exit(0)

def ensure(file, desc):
    if path.exists(file) == False:
        print(f"{desc} file not found: {file}")
        exit(1)

if path.splitext(argv[1])[1] in ['.ini','.txt']:

    if path.dirname(argv[1]) == '': # If no directory, look for it in the settings folder
        recipe = path.join(path.dirname(argv[0]), f"settings\\{argv[1]}")
        config_filepath = recipe
        conf = ConfigParser()
        conf.read(recipe)
    else: # If there is a directory, it's a full path so just load it in
        conf = ConfigParser()
        conf.read(argv[1])
        config_filepath = argv[1]

    queue = argv[2:]
else:
    recipe = path.join(path.dirname(argv[0]), "settings\\recipe.ini")
    config_filepath = recipe
    conf = ConfigParser()
    conf.read(recipe)
    queue = argv[1:]

for video in queue:

    if str(conf['misc']['flavors']) in [yes,'fruits']:
        flavors = [
            'Strawberry','Blueberry','Raspberry','Blackberry','Cherry','Cranberry','Coconut','Pineapple','Kiwi'
            'Peach','Apricot','Dragonfuit','Grapefruit','Melon','Papaya','Watermelon','Banana','Apple','Pear','Orange'
        ]
    else:
        flavors = ['Smoothie']

    filename, ext = path.splitext(video)

    if conf['misc']['folder'] in no:

        outdir = path.dirname(video)
    else:
        outdir = conf['misc']['folder']

    out = path.join(outdir, filename + f' - {choice(flavors)}' + ext)

    count=2
    while path.exists(out):
        out = path.join(outdir, filename + f' - {choice(flavors)}' + f' ({count})' + ext)
        count+=1

    command = [ # Split in two for readability
        
        f"cmd /c vspipe -y \"{path.join(path.dirname(argv[0]), 'blender.vpy')}\" --arg input_video=\"{video}\" --arg config_filepath=\"{config_filepath}\"",
        f"- | {conf['encoding']['process']} -hide_banner -loglevel warning -stats -i - {conf['encoding']['args']} \"{out}\""
        # -i \"{video}\" -map 0:v -map 1:a?
    ]
    if (conf['misc']['verbose']) in yes:
        print(command)
        print(f"VIDEO: {video}")

    run(' '.join(command),shell=True)