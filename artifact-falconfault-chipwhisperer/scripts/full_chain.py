from dataclasses import dataclass, field
from subprocess import run
from typing import List, Tuple
from pathlib import Path
import numpy as np
import run_attack

import chipw as chiputils
import os

FAULTSIM_EXE = os.environ['FAULTSIM_EXE']
INSTANCEGEN_EXE = os.path.dirname(FAULTSIM_EXE) + "/instancegen"
EXEC_DIR = os.getenv('ATTACK_EXEC_DIR', "./exec/")
NUM_KEYS_TO_ATTACK = os.getenv('NUM_KEYS', "10")

Command = chiputils.Command

@dataclass
class AttackInstance:
    # following fields should be fixed during every instance of the attack
    hexfile_path     : str       = chiputils.DEFAULT_HEXFILE # path to the hex to attack
    target_command   : Command = Command.sqrt
    nbsigs : int = 2_000_000 # nb sigs to try to carry the simulation of the attack
    dirname_template : str = "test_v0"
    glitch_config_file_path : str | None = None
    # following fields are meant to be mutable on an instance basis, evolving as the current instance under attack changes
    actual_dirname :str = ""   # name of the directory currently under attack
    sensitive_value  : int = 0 # leaf value of current instance (unglitched value)
    possible_glitch_values  : List[float] = field(default_factory=list)



def instance_gen(dirname : str) -> int:
    """ generates a FALCON-512 private key in EXEC_DIR + dirname together with the value we want to target in the FALCON-TREE 
        
        outputs the value on which we want to run the fpr_sqrt glitch on
    """
    run(["mkdir", "-p",  dirname])
    ret = run([INSTANCEGEN_EXE, dirname])
    if ret.returncode != 0:
        # do something?
        print("An error occured during keygen")
        return 0
    sv = np.fromfile(dirname + "/sensitive_value", dtype=np.double)
    sigma = 165.736617183 # FALCON-512
    return int((sigma * sv)[0] ** 2) # should be an integer, would be better to get it directly from FALCON code

#def perform_glitch(sensitive_value : int, reprogram : bool = False, hexfile_path : str = "", target_command : str = 's') ->  List[float]:
def perform_glitch(atck_inst : AttackInstance) ->  List[float]:
    """ runs the fpr_sqrt glitch on the sensitive_value on the STM32F4 board
        and outputs a list of glitched values
    """

    chiputils.setup_glitch()

    return chiputils.find_possible_glitch_values(atck_inst.sensitive_value, target_command=atck_inst.target_command, config_file_path=atck_inst.glitch_config_file_path)

def test_glitch_attack(atck_inst : AttackInstance) -> Tuple[List[float], List[float],List[float]]:
    """ for each value in possible_values, test the attack by reinjecting the glitched value in the FALCON-TREE

        outputs values for which we have a success? and the number of signatures
    """
    success_val = []
    success_oob_val = []
    close_enough = []
    for val in atck_inst.possible_glitch_values:
        retval = run([FAULTSIM_EXE, atck_inst.actual_dirname, str(val), str(atck_inst.nbsigs)])
        if retval.returncode == 0:
            success_val.append(val)
            return (success_val, success_oob_val, close_enough) # no need to go further, we have a perfect attack
        elif retval.returncode == 1:
            success_oob_val .append(val)
        elif retval.returncode == 2:
            close_enough.append(val)

    return (success_val, success_oob_val, close_enough)


# local code only
def instance_and_glitch(atck_inst : AttackInstance) -> List[float]:
    sv = instance_gen(atck_inst.actual_dirname)
    atck_inst.sensitive_value = sv
    l = perform_glitch(atck_inst)
    return l

def remote_chain(nb_keys : int, atck_inst : AttackInstance, verbose : bool = False):
    print(f"Programming: {atck_inst.hexfile_path} to target...")
    chiputils.program(atck_inst.hexfile_path) # ensure that the right program is running on the target
    for i in range(nb_keys):
        dirname = EXEC_DIR + f"{atck_inst.dirname_template}_{i}/"
        atck_inst.actual_dirname = dirname
        glitched_value_file = atck_inst.actual_dirname + "glitch_value"
        glitched_value_path = Path(glitched_value_file)
        if not glitched_value_path.exists():
            sv = instance_gen(atck_inst.actual_dirname)
            atck_inst.sensitive_value = sv
            l = np.array(perform_glitch(atck_inst))
            if verbose:
                print(l)
            l.tofile(glitched_value_file)

asm21_attack = AttackInstance(hexfile_path=chiputils.ASM21_HEXFILE, 
                            nbsigs=10_000_000,
                            dirname_template="asm21", 
                            target_command=Command.sqrt, 
                            glitch_config_file_path=EXEC_DIR + "asm21.glitch_config")

c21_attack = AttackInstance(hexfile_path=chiputils.C21_HEXFILE, 
                            nbsigs=10_000_000,
                            dirname_template="c21", 
                            target_command=Command.sqrt, 
                            glitch_config_file_path=EXEC_DIR + "c21.glitch_config")

asm25_attack = AttackInstance(hexfile_path=chiputils.ASMFNDSA_HEXFILE,
                            nbsigs=2_000_000, 
                            dirname_template="asm25",
                            target_command=Command.sqrt, 
                            glitch_config_file_path=EXEC_DIR + "asm25.glitch_config")


attacks = [asm21_attack, c21_attack, asm25_attack]

if __name__ == """__main__""":
    nbkeys = int(NUM_KEYS_TO_ATTACK)
    print("Starting all attacks...")
    for attack in attacks:
        remote_chain(nbkeys, attack, verbose=True)
        run_attack.do_attack(list(range(nbkeys)), attack.nbsigs, attack.dirname_template)
        run_attack.run_all_recovery(list(range(nbkeys)), attack.dirname_template, max_nbsigs=40_000_000)
