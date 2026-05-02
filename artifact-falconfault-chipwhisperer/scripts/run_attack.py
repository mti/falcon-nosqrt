from subprocess import run
from typing import List, Tuple
from pathlib import Path

import numpy as np
import os

# type GlitchAttackSuccess = Tuple[List[float], List[float],List[float]]

FAULTSIM_EXE = os.environ['FAULTSIM_EXE']
EXEC_DIR = os.getenv('ATTACK_EXEC_DIR', "./exec/")

def sort_glitch_value_by_score(possible_values : List[float]) -> List[float]:
    # sigma = 165.736617183
    smax = 1 / 1.27783
    smin = 1 / 1.8205
    epsilon = 0.01
    in_range = lambda x : (x < smax) and (x > smin)
    are_close = lambda x, y : (abs(x - y) < epsilon)
    # prefer value in range, if not in range prefer values close to smin
    score_func = lambda x : x - smin if in_range(x) else (smax - smin) + abs(x - smin)
    sorted_values = sorted(possible_values, key=score_func)
    # flag values that are too close to another value to get rid of them
    # we don't care about transitivity issues here
    curr_ind = 1
    final_values = [sorted_values[0]]
    while curr_ind < len(sorted_values):
        curr_val = sorted_values[curr_ind]
        prev_val = sorted_values[curr_ind - 1]
        if not are_close(curr_val, prev_val):
            final_values.append(curr_val)
        curr_ind += 1

    return final_values

def optimal_glitch_values(possible_values : List[float]) -> List[float]:
    smax = 1 / 1.27783
    smin = 1 / 1.8205
    in_range = lambda x : (x < smax) and (x > smin)
    min_in = lambda x : x - smin if in_range(x) else (smax - smin) + abs(x - smin)
    max_in = lambda x : smax - x if in_range(x) else (smax - smin) + abs(x - smin)
    max_under = lambda x : smin - x if 0 <= x < smin else 1000
    max_above = lambda x : 1/(x - smax) if smax+0.1 <= x < 8 else 1000 
    funcs = [min_in, max_in, max_under, max_above]
    optimal_values = set()
    for f in funcs:
        min_val  = -1
        min_eval = 999
        for val in possible_values:
            if f(val) < min_eval:
                min_val = val
                min_eval = f(val)
        if min_val >= 0.:
            optimal_values.add(min_val)
    lopt_values = list(optimal_values)
    for x in lopt_values:
        for y in lopt_values:
            if x != y and abs(x - y) < 0.01:
                lopt_values.remove(y)

    return lopt_values

def test_glitch_attack(possible_values : List[float], path_to_instance : str, nbsigs : int, sigstep : int = 20000) -> Tuple[List[float], List[float],List[float]]:
    """ for each value in possible_values, test the attack by reinjecting the glitched value in the FALCON-TREE

        outputs values for which we have a success
    """
    success_val = []
    success_oob_val = []
    close_enough = []
    sorted_values = optimal_glitch_values(possible_values)
    print("iterating through glitch values: ", sorted_values)
    for val in sorted_values:
        retval = run([FAULTSIM_EXE, path_to_instance, str(val), str(nbsigs), str(sigstep)])
        if retval.returncode == 0:
            success_val.append(val)
            return (success_val, success_oob_val, close_enough) # no need to go further, we have a perfect attack
        elif retval.returncode == 1:
            success_oob_val.append(val)
            return (success_val, success_oob_val, close_enough) # no need to go further, we sorted the value so next attacks will still be oob
        elif retval.returncode == 2:
            close_enough.append(val)

    return (success_val, success_oob_val, close_enough)

def do_attack(keys_id : List[int], nbsigs : int, dir_template : str = "test_v0"):
    success_id = []
    success_oob_id = []
    close_id = []
    nb_keys = len(keys_id)
    for i in keys_id:
        dirname = EXEC_DIR + f"{dir_template}_{i}/"
        val :List[float] = list(np.fromfile(dirname + "glitch_value", dtype=float))
        result_path = Path(dirname + "results.npz")
        rerun_flag = True
        (success_val, success_oob_val, close_val) = [], [], []
        if result_path.exists():
            print(f"results exist for {dirname}: checking if rerunning is necessary")
            result_data = np.load(result_path)
            more_sigs  = result_data['nbsigs'] < nbsigs
            no_new_val = np.array_equal(result_data['glitch_val'], val)
            success_val = result_data['success_val'] 
            success_oob_val = result_data['success_oob_val']
            close_val = result_data['close_val'] 
            close_miss = (len(success_val) == 0) and (len(close_val) > 0)
            if (not (more_sigs and close_miss)) and no_new_val:
                print(f"                          : no need to rerun the attack, reusing results")
                rerun_flag = False

        if rerun_flag:
            (success_val, success_oob_val, close_val) = test_glitch_attack(val, dirname, nbsigs)
            print(f"saving new results in {result_path}...")
            np.savez(result_path,
                     nbsigs=np.array(nbsigs),
                     success_val=np.array(success_val),
                     success_oob_val=np.array(success_oob_val),
                     close_val=np.array(close_val),
                     glitch_val=np.array(val))

        if len(success_val) > 0:
            success_id.append(i)
        elif len(close_val) > 0:
            close_id.append(i)
        elif len(success_oob_val) > 0:
            success_oob_id.append(i)
        
        
    print(f"Successful       recovery for {len(success_id)}/{nb_keys} keys.")
    print(f"Successful (oob) recovery for {len(success_oob_id) }/{nb_keys} keys.")
    print(f"Close            recovery for {len(close_id) }/{nb_keys} keys.")
    print(f"Total recovery: {(len(success_id) + len(success_oob_id) + len(close_id)) / nb_keys * 100 :.2f}%")
    
    return (success_id, success_oob_id, close_id)

def perform_recovery(path_to_instance_dir : str, max_nbsigs : int = 10_000_000, sigstep : int = 100_000):
    path_to_result   = Path(path_to_instance_dir + "results.npz")
    if not path_to_result.exists():
        print("No results to exploit, please run the analysis on the key first") # TODO, perform the analysis there if it's not
        print("or check that 'path_to_instance_dir' is correct")
        return -2
    result_data = np.load(path_to_result)
    success_val     = result_data['success_val'] 
    success_oob_val = result_data['success_oob_val']

    if len(success_val) > 0:
        retval = run([FAULTSIM_EXE, path_to_instance_dir, str(success_val[0]), str(max_nbsigs), str(sigstep)])
        return retval.returncode
    elif len(success_oob_val) > 0:
        retval = run([FAULTSIM_EXE, path_to_instance_dir, str(success_oob_val[0]), str(max_nbsigs), str(sigstep)])
        return retval.returncode
    print(f"No possible value in results file to perform key recovery on: {path_to_instance_dir}")
    return -1
    
def run_all_recovery(keys_id : List[int], template_path_to_instance_dir : str, rerun : bool = False, max_nbsigs : int = 10_000_000, sigstep : int = 10000):
    success_count = 0
    for id in keys_id:
        path_to_instance_dir = EXEC_DIR + f"{template_path_to_instance_dir}_{id}/"
        success_file = Path(path_to_instance_dir + "recovered")
        if success_file.exists() and not rerun:
            success_count += 1
            print("key already recovered previously, skipping...  (use rerun = True to force key recovery)")
            continue
        retval = perform_recovery(path_to_instance_dir, max_nbsigs=max_nbsigs, sigstep=sigstep)
        if retval == 0:
            success_count += 1
            success_file.touch() # create a success file
    print(f"recovered {success_count}/{len(keys_id)} ({success_count/len(keys_id) * 100:.2f}%) keys.")
