from dataclasses import dataclass
from enum import Enum
import itertools
from pathlib import Path
import struct
from typing import List, Optional
from tqdm import tqdm

import chipwhisperer as cw
import numpy as np
import os

scope = cw.scope()
target = cw.target(scope, cw.targets.SimpleSerial2)
prog = cw.programmers.STM32FProgrammer

PLATFORM = 'CW308_STM32F4'
ASMFNDSA_HEXFILE = os.environ['ASMFNDSA_HEXFILE']
ASM21_HEXFILE = os.environ['ASM21_HEXFILE']
C21_HEXFILE = os.environ['C21_HEXFILE']
DEFAULT_HEXFILE = ASMFNDSA_HEXFILE

# simple serial target commands
class Command(Enum):
    sqrt = "s"
    mult = "m"

import time
time.sleep(0.05)
scope.default_setup()

def reboot_flush():            
    scope.io.nrst = False
    time.sleep(0.05)
    scope.io.nrst = "high_z"
    time.sleep(0.05)
    #Flush garbage too
    target.flush()

def program(hexfile_path=DEFAULT_HEXFILE):
    cw.program_target(scope, prog, hexfile_path)
    target.reset_comms()

def setup_glitch():
    scope.cglitch_setup()
    cw.glitch_logger.setLevel(cw.logging.ERROR)

def disconnect():
    scope.dis()
    target.dis()

@dataclass
class GlitchParamSet():
    command    : Command = Command.sqrt
    offset     : int = -11
    width      : int = 11
    ext_offset : int = 13


def glitch(value : int, glitch_params : GlitchParamSet = GlitchParamSet()) -> Optional[float]:
    scope.glitch.repeat = 1
    scope.glitch.offset = glitch_params.offset
    scope.glitch.width  = glitch_params.width
    scope.glitch.ext_offset = glitch_params.ext_offset
    scope.sc._timeout = 10
    target_command = glitch_params.command

    query = struct.pack("<d",value)

    target.reset_comms()
    scope.arm()
    
    target.simpleserial_write(target_command.value, query)
    ret = scope.capture()
    
    val = target.simpleserial_read_witherrors('r', 8, glitch_timeout=10)

    if ret or not val['valid']:
        reboot_flush()
        return None
    else:
        v = struct.unpack("<d", val['payload'])[0]
        if glitch_params.command == Command.sqrt:
            sigma=165.736617183
            faulty_val = v / sigma
            if faulty_val < 8: # remove stupid values
                return faulty_val
        if glitch_params.command == Command.mult:
            return v

    return None



def find_possible_glitch_values(value : int, target_command : Command = Command.sqrt, config_file_path : str | None = None) -> List[float]:
    ext_offset = list(range(1,300)) #[13, 25, 185, 274, 290]
    widths = [12] #[1, 11, 10, 12]
    offsets = [-14] #[-4, -11, -12, -14]

    possible_values = set()
    save_results = False
    useful_parameters = [] # only used to save values
    parameter_triplet = itertools.product(ext_offset, widths, offsets)
    
    if config_file_path != None:
        if not config_file_path.endswith(".npy"):
            config_file_path += ".npy"

        if not Path(config_file_path).exists():
            save_results = True
        else:
            parameter_triplet = np.load(config_file_path)



    for (ext, wi, off) in tqdm(parameter_triplet):
        glitch_params = GlitchParamSet(offset=off, width=wi, ext_offset=ext, command=target_command)
        glitched_value : Optional[float] = glitch(value, glitch_params)
        if glitched_value != None:
            possible_values.add(glitched_value)
            epsilon = 0.001
            if save_results and abs(value - glitched_value) > epsilon:
                useful_parameters.append((ext, wi, off))
    
    if save_results and len(useful_parameters) >= 1 and config_file_path!= None: # last check is just for the typechecker
        arr = np.array(useful_parameters, dtype=np.int16)
        np.save(config_file_path, arr)
        
    return sorted(list(possible_values))


def poll_target_until(endmsg):
    read_data = ""
    while not(read_data.__contains__(endmsg)): # why not
        read_now = target.read(timeout=100)
        read_data += read_now
        
    return read_data
