import wmi
def GetGPUs():
    GPU_1 = wmi.WMI().Win32_VideoController()[0].Name
    try:
        GPU_2 = wmi.WMI().Win32_VideoController()[1].Name
    except:
        GPU_2 = None
    GPU_List=(GPU_1,GPU_2)
    return GPU_List

def Settings():
    List=GetGPUs()
    if List[1] is not None:
        print("\nSelect a GPU to encode with:")
        print("0:", List[0]+"\n"+"1:", List[1])
        while True:
            try:
                Index=int(input("\nSelect GPU by ID: "))
                if Index not in [0,1] or type(Index) is not int:
                    continue
                else:
                    print("\nSelected:",List[Index]+"\n")
                    break
                
            except:
                continue
    else:
        Index=0                  

    if "nvidia" in List[Index].lower():
        HWAccelArgs = "-hwaccel cuda -threads 4"
        EncodingArgs = "-c:v hevc_nvenc -rc constqp -preset p7 -qp 18"

    elif "amd" in List[Index].lower() or "vega" in List[Index].lower() or "radeon" in List[Index].lower():
        HWAccelArgs = "-hwaccel d3d11va -threads 4"
        EncodingArgs = "-c:v hevc_nvenc -rc constqp -preset p7 -qp 18"

    elif "intel" in List[Index].lower():
        HWAccelArgs = "-hwaccel d3d11va -threads 4"
        EncodingArgs = "-c:v hevc_qsv -preset veryslow -global_quality:v 18"
    else:
        HWAccelArgs="-threads 4"
        EncodingArgs = "-c:v libx265 -preset medium -crf 18"

    return ('-y '+HWAccelArgs+' -loglevel error -hide_banner -stats -i {Input} '+EncodingArgs+' {Output}')   







