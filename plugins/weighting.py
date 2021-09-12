import math

def scaleWeights(frames):
    tot = sum(frames)
    return [frame / tot for frame in frames]

# modified functions taken from https://github.com/siveroo/HFR-Resampler

# returns a list of values like below:
# [0, 1, 2, 3, ..., frames] -> [a, ..., b]
def scaleRange(frames, a, b):
    return [(x * (b - a) / (frames - 1)) + a for x in range(0, frames)]
    
def equal(frames):
    return [1 / frames] * frames

def gaussian(frames, standard_deviation = 2, bound = [0, 2]):
    r = scaleRange(frames, bound[0], bound[1])
    val = [math.exp(-((x) ** 2) / (2 * (standard_deviation ** 2))) for x in r]
    return scaleWeights(val)

def gaussianSym(frames, standard_deviation = 2, bound = [0, 2]):
    max_abs = max(bound)
    r = scaleRange(frames, -max_abs, max_abs)
    val = [math.exp(-((x) ** 2) / (2 * (standard_deviation ** 2))) for x in r]
    return scaleWeights(val)

def pyramid(frames, reverse = False):
    val = []
    if reverse:
        val = [x for x in range(frames, 0, -1)]
    else:
        val = [x for x in range(1, frames + 1)]
    return scaleWeights(val)

def pyramidSym(frames):
    val = [((frames - 1) / 2 - abs(x - ((frames - 1) / 2)) + 1) for x in range(0, frames)]
    return scaleWeights(val)

def funcEval(func, nums):
    try:
        return eval(f"[({func}) for x in nums]")
    except NameError as e:
        raise InvalidCustomWeighting

def custom(frames, func = "", bound = [0, 1]):
    r = scaleRange(frames, bound[0], bound[1])
    val = funcEval(func, r)
    if min(val) < 0: val -= min(val)
    return scaleWeights(val)

# stretch the given array (weights) to a specific length (frames)
# example : frames = 10, weights = [1,2]
# result : val = [1, 1, 1, 1, 1, 2, 2, 2, 2, 2], then normalize it to [0.0667,
# 0.0667, 0.0667, 0.0667, 0.0667, 0.1333, 0.1333, 0.1333, 0.1333, 0.1333]
def divide(frames, weights):
    r = scaleRange(frames, 0, len(weights) - 0.1)
    val = []
    for x in range(0, frames):
        scaled_index = int(r[x])
        val.append(weights[scaled_index])

    if min(val) < 0: val -= min(val)

    return scaleWeights(val)