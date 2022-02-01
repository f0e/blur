from sys import argv # parse args
from os import _exit, path # split file extension
from yaml import load,FullLoader # parse the config
from subprocess import run # Run vs
from random import choice # Randomize the smoothie's flavor))

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

if path.splitext(argv[1])[1] in ['.conf','.cfg','.txt','.json','.yml','yaml']:

    if path.dirname(argv[1]) == '': # If no directory, look for it in the settings folder
        recipe = path.join(path.dirname(argv[0]), f"settings\\{argv[1]}")
        config_filepath = recipe
        conf = load(open(recipe), Loader=FullLoader)
    else: # If there is a directory, it's a full path so just load it in
        conf = load(open(argv[1]), Loader=FullLoader)
        config_filepath = argv[1]

    queue = argv[2:]
else:
    recipe = path.join(path.dirname(argv[0]), "settings\\recipe.yaml")
    config_filepath = recipe
    conf = load(open(recipe), Loader=FullLoader)
    queue = argv[1:]

for video in queue:

    if ['conf']['misc']['random flavors'] == True:
        flavors = [
            'Strawberry'
            'Blueberry'
            'Raspberry'
            'Blackberry'
            'Cherry'
            'Cranberry'
            'Coconut'
            'Peach'
            'Apricot'
            'Dragonftui'
            'Grapefruit'
            'Melon'
            'Papaya'
            'Watermelon'
            'Banana'
            'Pineapple'
            'Apple'
            'Kiwi'
        ]
    else:
        flavors = ['Smoothie']

    filename, ext = path.splitext(video)

    if conf['misc']['custom output folder'] in [None,'null','','none','no','n']:

        outdir = path.dirname(video)
    else:
        outdir = conf['misc']['custom output folder']

    out = path.join(outdir, filename + f'- {choice(flavors)}' + ext)

    count=2
    while path.exists(out):
        out = path.join(outdir, filename + f'- {choice(flavors)}' + f' ({count})' + ext)
        count+=1

    command = [ # Split in two for readability
        f"vspipe -i \"{path.join(path.dirname(argv[0]), 'blender.vpy')}\" --arg input_video=\"{video}\" --arg config_filepath=\"{config_filepath}\" --container y4m",
        f"-| {conf['encoding']['process']} -hide_banner -loglevel warning -stats -i - {conf['encoding']['args']} \"{out}\""
    ]
    print(command)

    run(' '.join(command),shell=True)
