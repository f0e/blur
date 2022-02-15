from argparse import ArgumentParser
from sys import argv,exit
from os import path, system
from configparser import ConfigParser
from subprocess import run
from random import choice # Randomize smoothie's flavor

# Bool aliases
yes = ['True','true','yes','y','1']
no = ['False','false','no','n','0','null','',None]

parser = ArgumentParser()
parser.add_argument("-peek",    "-p",    help="render a specific frame (outputs an image)", action="store", nargs=1, metavar='752',       type=int)
parser.add_argument("-trim",    "-t",    help="Trim out the frames you don't want to use",  action="store", nargs=1, metavar='0:23,1:34', type=str)
parser.add_argument("-dir",              help="opens the directory where Smoothie resides", action="store_true"                                   )
parser.add_argument("-recipe",  "-rc",   help="opens default recipe.ini",                   action="store_true"                                   )
parser.add_argument("--config", "-c",    help="specify override config file",               action="store", nargs=1, metavar='PATH',      type=str)
parser.add_argument("--encoding","-enc", help="specify override ffmpeg encoding arguments", action="store",                               type=str)
parser.add_argument("-verbose", "-v",    help="increase output verbosity",                  action="store_true"                                   )
parser.add_argument("-curdir",  "-cd",   help="save all output to current directory",       action="store_true",                                  )
parser.add_argument("-input",   "-i",    help="specify input video path(s)",                action="store", nargs="+", metavar='PATH',    type=str)
parser.add_argument("-vpy",              help="specify a VapourSynth script",               action="store", nargs=1, metavar='PATH',      type=str)
args = parser.parse_args()

if args.dir:
    run(f'explorer {path.dirname(argv[0])}')
    exit(0)
if args.recipe:
    recipe = path.abspath(path.join(path.dirname(__file__), "settings/recipe.ini"))
    if path.exists(recipe) == False:
        print("config path does not exist (are you messing with files?), exitting")
        run('powershell -NoLogo')
    run(f'explorer {recipe}')
    exit(0)


conf = ConfigParser()

if args.config:
    config_filepath = path.abspath(args.config[0])
    conf.read(config_filepath)
else:
    config_filepath = path.abspath(path.join(path.dirname(__file__), "settings/recipe.ini"))
    conf.read(config_filepath)

if path.exists(config_filepath) in [False,None]:
    print("config path does not exist, exitting")
    run('powershell -NoLogo')
elif args.verbose:
    print(f"VERBOSE: using config file: {config_filepath}")

if args.input in [no, None]:
    parser.parse_args('-h'.split()) # If the user does not pass any args, just redirect to -h (Help)

round = 0 # Reset the round counter

for video in args.input: # Loops through every single video

    # Title

    round += 1

    title = "Smoothie - " + path.basename(video)

    if len(args.input) > 1:
        title = f'[{round}/{len(args.input)}] ' + title

    system(f"title {title}")

    # Suffix

    if str(conf['misc']['flavors']) in [yes,'fruits']:
        flavors = [
            'Berry','Cherry','Cranberry','Coconut','Kiwi','Avocado','Durian','Lemon','Lime','Fig','Mirabelle',
            'Peach','Apricot','Grape','Melon','Papaya','Banana','Apple','Pear','Orange','Mango','Plum','Pitaya'
        ]
    else:
        flavors = ['Smoothie']

    # Extension

    if args.peek:
        ext = '.png'
    elif conf['misc']['container'] in no:
        ext = path.splitext(video)[1]
    else:
        ext = conf['misc']['container']

    filename = path.basename(path.splitext(video)[0])

    # Directory

    if args.curdir:
        outdir = path.abspath(path.curdir)
    elif conf['misc']['folder'] in no:
        outdir = path.dirname(video)
    else:
        outdir = conf['misc']['folder']

    out = path.join(outdir, filename + f' - {choice(flavors)}{ext}')

    count=2
    while path.exists(out):
        out = path.join(outdir, f'{filename} - {choice(flavors)} ({count}){ext}')
        count+=1

    # VapourSynth

    vspipe = path.join(path.dirname((path.dirname(__file__))),'VapourSynth','VSPipe.exe')

    if args.vpy:

        if path.dirname(args.vpy[0]) in no:
            
            vpy = path.join( path.dirname(__file__), (args.vpy[0]) )
        else:
            vpy = path.abspath(args.vpy[0])
    else:
        vpy = path.abspath(path.join(path.dirname(__file__),'blender.vpy'))
    
    command = [ # This is the master command, it gets appended some extra output args later down
    f'{vspipe} -y "{vpy}" --arg input_video="{video}" --arg config_filepath="{config_filepath}" ',
    f'- | {conf["encoding"]["process"]} -hide_banner -loglevel warning -stats -i - ',
    ]

    if args.peek:
        frame = int(args.peek[0]) # Extracting the frame passed from the singular array
        command[0] += f'--start {frame} --end {frame}'
        command[1] += f' "{out}"' # No need to specify audio map, simple image output
    elif args.trim:
        command[0] += f'--arg trim="{args.trim}"'
        command[1] += f'{conf["encoding"]["args"]} "{out}"'
    else:
        # Adds as input the video to get it's audio tracks and gets encoding arguments from the config file
        command[1] += f'-i "{path.abspath(video)}" -map 0:v -map 1:a? {conf["encoding"]["args"]} "{out}"'

    if args.verbose:
        command[0] += ' --arg verbose=True'
        for cmd in command: print(cmd)
        print(f"Queuing video: {video}")

    run(' '.join(command),shell=True)

system(f"title [{round}/{len(args.input)}] Smoothie - Finished! (EOF)")