# Artifact 3: ChipWhisperer Fault-Attack Instrumentation

This artifact contains the code and Nix build glue used to reproduce the physical fault-injection part of the Falcon square-root attack. It builds the target firmware variants, provides a Python/ChipWhisperer environment, and connects the collected faulty outputs to the modified-leaf key-recovery simulator from the second artifact.

The artifact targets a ChipWhisperer Pro / CW1200 setup with a CW308 STM32F4 target board by default. The target platform is set in `flake.nix` as:

```nix
victimPlatform = "CW308_STM32F4";
```

The default configuration builds three firmware variants:

- `glitch_falcon2021_cm4asm`: Falcon reference implementation, 2021 code, Cortex-M4 assembly floating-point emulation enabled.
- `glitch_falcon2021_c`: Falcon reference implementation, 2021 code, C floating-point emulation.
- `glitch_falcon2025_cm4asm`: newer Cortex-M4 assembly implementation from `c-fn-dsa`.

The flake also provides a development shell with ChipWhisperer, Jupyter, and the Python dependencies needed by the scripts.

## Repository layout

The artifact is expected to contain at least:

```text
flake.nix
flake.lock
chipwhisperer.nix
firmware/
  falconimpl_2021/
  falconimpl_cm4_2025/
scripts/
  chipw.py
  full_chain.py
  server_side_attack.py
```

This flake depends on the second artifact through:

```nix
artifact-leaffaultsim.url = "path:../artifact-leaffaultsim";
```

Therefore, the directory containing this artifact should be placed next to the `artifact-leaffaultsim` directory, or the path in `flake.nix` should be adjusted accordingly.

## Building the target firmware

To build the default firmware package:

```sh
nix build
```

By default, this builds the first firmware variant in the matrix. The resulting firmware files will appear under:

```text
result/bin/
```

For example, the `.hex` file has a name of the form:

```text
glitch_fndsa-CW308_STM32F4.hex
```

To build a specific firmware variant:

```sh
nix build .#glitch_falcon2021_cm4asm
nix build .#glitch_falcon2021_c
nix build .#glitch_falcon2025_cm4asm
```

The patched/build source files for the firmware are installed under:

```text
result/share/falcon-artifact/source/
```

This can be useful for checking exactly which Falcon implementation and build configuration were used.

The packaged ChipWhisperer Python package itself can also be built with:

```sh
nix build .#chipwhisperer
```

## Running the full attack chain

Enter the development shell:

```sh
nix develop
```

The shell sets the following environment variables:

```text
ASMFNDSA_HEXFILE   path to the Falcon 2025 Cortex-M4 assembly firmware
ASM21_HEXFILE      path to the Falcon 2021 Cortex-M4 assembly firmware
C21_HEXFILE        path to the Falcon 2021 C-emulation firmware
PLATFORM           ChipWhisperer target platform, default CW308_STM32F4
FAULTSIM_EXE       path to the modified-leaf key-recovery simulator
NUM_KEYS           number of generated/attacked keys per implementation
```

The shell also copies the ChipWhisperer Jupyter tutorials and documentation to:

```text
./cw_jupyter
```

With the ChipWhisperer and target board connected, run:

```sh
python3 scripts/full_chain.py
```

This script attacks the three target implementations in sequence, using the firmware paths exported by the development shell. It generates Falcon keys, performs the glitch attack against the target firmware, and invokes the key-recovery simulator from `artifact-leaffaultsim` on the resulting faulty instances. Attack results are collected in the `./exec` repository by default, but this can be changed using the `ATTACK_EXEC_DIR` environment variable.

The default number of keys is:

```sh
NUM_KEYS=10
```

To change it after entering the shell:

```sh
export NUM_KEYS=50
python3 scripts/full_chain.py
```

## Interactive use

For inspection, debugging, or adapting the glitch search, enter the shell and launch Jupyter:

```sh
nix develop
jupyter lab
```

The local `cw_jupyter/` directory contains the ChipWhisperer notebooks copied from the packaged ChipWhisperer source. The scripts in `scripts/` can also be run directly from the shell.

## Reproducibility notes

The Nix flake pins the Falcon sources, the ChipWhisperer source, the ARM embedded toolchain, Python packages, and the key-recovery simulator dependency through `flake.lock`. For artifact review, use:

```sh
nix build --no-update-lock-file
nix develop --no-update-lock-file
```

and avoid running `nix flake update`, since that would intentionally change the pinned dependency set.

This artifact is nevertheless hardware-dependent. The following points are important for reproducing the physical glitching results.

### Identical target board

The currently hardcoded glitch parameters are expected to remain valid for an identical model of the target board and microcontroller. However, small manufacturing or clocking differences can affect the glitch window. If the attack does not trigger reliably, the first parameters to adjust are the `widths` and `offsets` ranges in:

```text
scripts/chipw.py
```

specifically in the function:

```text
find_possible_glitch_values
```

A small local search around the provided values is usually the first troubleshooting step.

### Different STM32F target board

For a different STM32F target board, update the platform variable in `flake.nix`:

```nix
victimPlatform = "CW308_STM32F4";
```

to the appropriate ChipWhisperer platform name.

A new target board will almost certainly require a new search for glitch parameters. In particular, the `widths` and `offsets` ranges in `scripts/chipw.py` should be treated as board-specific tuning parameters rather than universal constants.

### ChipWhisperer-Husky

The scripts were written and tested for the ChipWhisperer Pro / CW1200 setup. The attack should extend to the newer ChipWhisperer-Husky, but this requires adapting the glitch-control code to the Husky API and clock-glitching model.

The relevant differences are discussed in NewAE's Husky material, in particular the Husky section of the ChipWhisperer fault101 clock-glitching tutorial. We did not have access to a Husky device and therefore cannot confirm the required adaptations experimentally.

### USB permissions

On Linux, access to the ChipWhisperer device may require appropriate udev rules or equivalent permissions. See the [`.rules` file provided by ChipWhisperer](https://github.com/newaetech/chipwhisperer/blob/develop/50-newae.rules) for details.

## Typical workflow

A reviewer using the default CW308 STM32F4 target can run:

```sh
# Build firmware, mainly to check that the target code compiles.
nix build .#glitch_falcon2025_cm4asm

# Enter the reproducible Python/ChipWhisperer environment.
nix develop

# Optional: use fewer keys for a quick smoke test.
export NUM_KEYS=1

# Run the physical glitching and key-recovery chain.
python3 scripts/full_chain.py
```

For a more complete reproduction, increase `NUM_KEYS` and run the full chain on all three firmware variants with the default script.
