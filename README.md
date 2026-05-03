# Falcon Square-Root Artifacts

This repository contains the artifact code accompanying the paper on implementation issues caused by Falcon/FN-DSA's square-root computations.

The repository is split into four independent but related artifacts:

1. `artifact-falcon-nosqrt/`  
   Patch and build glue for the square-root-free implementation of Falcon, together with benchmarking and self-test targets.

2. `artifact-leaffaultsim/`  
   C++ simulation and statistical key-recovery code for the attack that starts from a modified Falcon-tree leaf value.

3. `artifact-falconfault-chipwhisperer/`  
   ChipWhisperer firmware, Python scripts, and Nix environment for the physical clock-glitch attack against Falcon square-root implementations on STM32F4/Cortex-M4 targets.

4. `artifact-supplysim/`  
   Supply-chain/dependency-management proof of concept using a wrapped `sqrt()` implementation, cross-compilation, and QEMU user-mode emulation.

Each artifact directory has its own `README.md`, `flake.nix`, and `flake.lock`. The root README gives only an overview and installation instructions; the detailed reproduction steps are in the artifact-specific READMEs.

## Supported platform

The artifacts are expected to build and run on Linux. They have primarily been tested on `x86_64-linux` with Nix flakes enabled.

Native macOS builds are **not currently supported**. Some parts of the artifacts assume a GNU/Linux-style toolchain and linker behavior, and the cross-compilation/QEMU setup in the supply-chain artifact is also Linux-oriented. In preliminary tests, adding GCC/G++/GNU binutils through Nix on macOS was not sufficient to make all binaries build and run correctly.

On macOS or other non-Linux hosts, we recommend using a Linux virtual machine and running the artifact commands inside that VM. On Windows, WSL may also work in principle, but this has not been tested by us; a conventional Linux VM is the safer recommendation for artifact review.

## Repository layout

```text
artifact-falcon-nosqrt/
  README.md
  flake.nix
  flake.lock
  patches/nosqrt.patch

artifact-leaffaultsim/
  README.md
  flake.nix
  flake.lock
  src/
    FalconKey.cpp
    FalconKey.h
    Makefile.leaffaultsim
    instancegen.cpp
    leaffaultsim.cpp
    vrfy_mq.patch

artifact-falconfault-chipwhisperer/
  README.md
  flake.nix
  flake.lock
  chipwhisperer.nix
  firmware/
    falconimpl_2021/
    falconimpl_cm4_2025/
  scripts/
    chipw.py
    full_chain.py
    run_attack.py

artifact-supplysim/
  README.md
  flake.nix
  flake.lock
  src/
    Makefile.supplysim
    fpr-optnativesqrt.patch
    supply_test.cpp
    wrap_sqrt_poc.c
```

## Installing Nix with flake support

The artifacts are packaged as Nix flakes. Flakes are used through the newer `nix build`, `nix run`, and `nix develop` interface. Nix still treats flakes as an experimental feature, so they must be enabled explicitly unless the installer does so for you. The official Nix documentation describes one-command use via `--experimental-features 'nix-command flakes'`, and permanent enablement through Nix settings. [nix.dev flakes documentation](https://nix.dev/concepts/flakes.html)

### Option A: Determinate Nix Installer

For users who do not already have Nix installed on Linux, the simplest route is usually the Determinate Systems installer. It installs Nix with flakes enabled by default. The installer itself also supports macOS, but these artifacts are only expected to work on Linux; macOS users should install and run Nix inside a Linux VM for artifact review. [Determinate Nix Installer](https://github.com/DeterminateSystems/nix-installer)

```sh
curl --proto '=https' --tlsv1.2 -sSf -L \
  https://install.determinate.systems/nix \
  | sh -s -- install
```

After installation, restart your shell or follow the installer instructions, then check:

```sh
nix --version
nix flake --help
```

### Option B: Existing Nix installation

If Nix is already installed but flakes are not enabled, either pass the feature flags explicitly:

```sh
nix --experimental-features 'nix-command flakes' build
nix --experimental-features 'nix-command flakes' run
```

or enable them permanently for your user by creating or editing:

```text
~/.config/nix/nix.conf
```

with:

```text
experimental-features = nix-command flakes
```

Then start a new shell and check:

```sh
nix flake --help
```

On NixOS, the equivalent system configuration is typically:

```nix
nix.settings.experimental-features = [ "nix-command" "flakes" ];
```

followed by a system rebuild. [nix.dev flakes documentation](https://nix.dev/concepts/flakes.html)

## General usage

Each artifact is self-contained. Enter the corresponding directory and use the commands described in its README.

For example, to build and run the square-root-free Falcon benchmark artifact on Linux:

```sh
cd artifact-falcon-nosqrt
nix build
nix run
```

For artifact review, the same commands can be run with `--no-update-lock-file` to ensure that the shipped lock files are not modified:

```sh
nix build --no-update-lock-file
nix run --no-update-lock-file
```

To inspect the exact package and app names exported by an artifact:

```sh
nix flake show
```

To enter a development shell, when provided:

```sh
nix develop
```

## Artifact summaries

### 1. Square-root-free Falcon implementation

Directory:

```sh
cd artifact-falcon-nosqrt
```

This artifact contains the patch against the Falcon reference implementation that removes square roots from signing, plus Nix glue to build and compare the baseline and square-root-free implementations.

It builds configurations for:

- baseline Falcon;
- square-root-free Falcon;
- floating-point emulation (`fpemu`);
- native floating-point (`fpnative`);
- AVX2, on supported x86_64 machines.

Typical commands:

```sh
nix build
nix run
nix run .#speed-baseline-fpnative
nix run .#test_falcon-nosqrt-fpemu
```

After `nix build`, the patched source tree can be inspected under:

```text
result/share/falcon-artifact/source/
```

See `artifact-falcon-nosqrt/README.md` for details.

### 2. Modified-leaf statistical key recovery

Directory:

```sh
cd artifact-leaffaultsim
```

This artifact contains the C++ code that simulates the statistical attack once a Falcon-tree leaf value has been modified. It links against the Falcon code and uses Eigen, provided by nixpkgs, for linear algebra.

Typical commands:

```sh
nix build
nix run
```

The artifact also exposes helper binaries for generating attack instances and running the leaf-fault simulator. See `artifact-leaffaultsim/README.md` for the exact app names and command-line arguments.

### 3. ChipWhisperer physical fault attack

Directory:

```sh
cd artifact-falconfault-chipwhisperer
```

This artifact contains the target firmware and Python/ChipWhisperer scripts used for the physical clock-glitch attack on STM32F4/Cortex-M4 targets.

Typical commands:

```sh
nix build
nix develop
python3 scripts/full_chain.py
```

This artifact requires physical hardware. The hardcoded glitch parameters are expected to work on an identical target board, but may require tuning for a different microcontroller sample or a different STM32F target. See `artifact-falconfault-chipwhisperer/README.md` for hardware, reproducibility, and ChipWhisperer-Husky notes.

### 4. Supply-chain / `sqrt()` dependency PoC

Directory:

```sh
cd artifact-supplysim
```

This artifact builds a Falcon-based test program together with a wrapped `sqrt()` implementation. It demonstrates when Falcon reaches a link-visible library `sqrt()` and when the call is bypassed by compiler builtins, target instructions, or Falcon's handwritten architecture-specific code.

Typical commands:

```sh
nix build
nix run
nix run .#native-ns -- key1
nix run .#riscv64-nb -- key1
```

Cross-target apps are wrapped with QEMU user-mode emulation where appropriate. The default smoke test intentionally avoids the expensive statistical key-recovery step unless requested with:

```sh
RUN_LEAFFAULTSIM=1 nix run
```

See `artifact-supplysim/README.md` for target/configuration names and the key-selection behavior of the proof of concept.

## Reproducibility notes

Each artifact ships its own `flake.lock`. This pins the nixpkgs revision and other flake inputs used for that artifact. For reproducible review runs, avoid:

```sh
nix flake update
```

unless intentionally updating the dependency set.

The usual caveats still apply:

- the artifacts are expected to run on Linux; on macOS or other non-Linux hosts, use a Linux VM;
- physical fault-injection results depend on the target board, clocking, cabling, and ChipWhisperer model;
- cross-compilation targets may require building large toolchains if binary substitutes are unavailable;
- QEMU user-mode behavior depends on the QEMU version pinned by nixpkgs and the host architecture;
- benchmark numbers depend on the reviewer machine and CPU frequency behavior.

## Recommended order for artifact review

A reviewer who wants a quick overview can proceed in this order:

```sh
cd artifact-falcon-nosqrt
nix run

cd ../artifact-leaffaultsim
nix run

cd ../artifact-supplysim
nix run

cd ../artifact-falconfault-chipwhisperer
nix develop
```

The ChipWhisperer artifact requires hardware for a full run; `nix build` only checks that the firmware/environment can be built.

## License and upstream code

The artifacts build on the Falcon reference implementation and ChipWhisperer-related code. Upstream source trees and patches are kept separate where possible so that copyright notices remain attached to the original code. The artifact-specific READMEs describe what is built and where the patched source trees can be inspected after `nix build`.
