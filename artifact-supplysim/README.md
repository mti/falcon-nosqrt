# Artifact 4: Falcon `sqrt` Supply-Chain / Dependency PoC

This artifact contains the proof-of-concept code for the supply-chain and dependency-management issue discussed in the paper. It builds a small Falcon-based test program, `supply_test`, together with the Falcon reference implementation and a wrapped `sqrt` routine. The goal is to demonstrate when Falcon's native floating-point square-root computation reaches a link-visible `sqrt` implementation, and when it is instead bypassed by compiler builtins, target instructions, or Falcon's handwritten architecture-specific square-root code.

The artifact is packaged as a Nix flake. The flake builds a matrix of target architectures and compiler/Falcon configurations, and exposes each configuration both as a buildable package and as a runnable app. For non-native targets, the app wrapper runs the resulting binary under QEMU user-mode emulation.

The artifact depends on the modified-leaf key-recovery simulator from artifact 2 through:

```nix
artifact-leaffaultsim.url = "path:../artifact-leaffaultsim";
```

Therefore this artifact directory should be placed next to `artifact-leaffaultsim`, or the path in `flake.nix` should be adjusted.

## Repository layout

The artifact is expected to contain at least:

```text
flake.nix
flake.lock
src/
  Makefile.supplysim
  supply_test.cpp
  wrap_sqrt_poc.c
  fpr-optnativesqrt.patch
  ...
```

The Falcon source tree is obtained from artifact 2 at build time, from:

```text
artifact-leaffaultsim/share/falcon-artifact/source/
```

and then patched or compiled according to the selected configuration.

## Configuration matrix

The flake defines target metadata in `targetInfo`. Each target records:

- the target label;
- the package set or cross package set used for compilation;
- the QEMU architecture used to run non-native binaries;
- whether the Falcon reference code contains handwritten square-root code for that target.

The current targets are:

```text
native
i686
aarch64
powerpc
powerpc64le
riscv64
loongarch64
```

There is also a commented-out `m68k` target, which can be enabled if the pinned nixpkgs revision and host machine can build the corresponding cross toolchain.

For each target, the flake generates the following configurations:

```text
<target>-default
<target>-nb
```

where:

- `<target>-default` uses the default Falcon/compiler behavior;
- `<target>-nb` builds with `-fno-builtin-sqrt`.

For targets where Falcon has handwritten architecture-specific native square-root code, the flake also generates:

```text
<target>-ns
```

where:

- `<target>-ns` applies the patch that forces Falcon's generic native `sqrt()` path and also builds with `-fno-builtin-sqrt`.

The `-ns` configuration is the one that models the vulnerable dependency path on targets where Falcon would otherwise bypass libm through architecture-specific code.

## Building

To build the default package:

```sh
nix build
```

The default package is:

```text
native-ns
```

The resulting files appear under:

```text
result/
```

The executable is:

```text
result/bin/supply_test
```

The exact patched source tree used for the build is installed under:

```text
result/share/falcon-artifact/source/
```

The build configuration is recorded in:

```text
result/share/falcon-artifact/config.txt
```

For example:

```sh
cat result/share/falcon-artifact/config.txt
```

To build individual configurations:

```sh
nix build .#native-default
nix build .#native-nb
nix build .#native-ns

nix build .#riscv64-default
nix build .#riscv64-nb

nix build .#aarch64-default
nix build .#aarch64-nb
nix build .#aarch64-ns
```

Cross targets may require building a complete cross toolchain if no binary substitute is available. This can take a significant amount of time.

## Running the default smoke test

The default app runs a small native smoke-test subset:

```sh
nix run
```

By default, this generates five key directories, `key1`, ..., `key5`, in the current working directory, then runs the native configurations on each key.

**Warning.** The default script overwrites existing directories whose names match this pattern. If you want to inspect generated keys later, copy them elsewhere or rename them to something that does not match `key<number>` before rerunning the script.

The number of keys can be changed with:

```sh
NUM_KEYS=1 nix run
```

The default runner prints the modified Falcon-tree leaf value produced by `supply_test`. It does **not** run the full statistical key-recovery simulator by default, because that step can be slow on a reviewer machine.

To enable key recovery:

```sh
RUN_LEAFFAULTSIM=1 nix run
```

The key-recovery parameters can be adjusted with:

```sh
RUN_LEAFFAULTSIM=1 \
NUM_KEYS=1 \
MAX_SIGS=3000000 \
PER_CORE_BATCH_SIZE=20000 \
nix run
```

The default runner currently includes only the native smoke-test configurations, so that plain `nix run` does not force expensive cross-toolchain builds. Cross-target configurations should normally be run through their individual apps, as described below.

## Running individual configurations

Each generated configuration is exposed as an app. The app takes a key directory as its argument.

For example:

```sh
nix run .#native-default -- key1
nix run .#native-nb -- key1
nix run .#native-ns -- key1
```

For cross targets, the app automatically wraps the target binary with QEMU user-mode emulation:

```sh
nix run .#riscv64-default -- key1
nix run .#riscv64-nb -- key1

nix run .#loongarch64-default -- key1
nix run .#loongarch64-nb -- key1

nix run .#aarch64-ns -- key1
nix run .#powerpc64le-ns -- key1
```

A typical manual workflow is:

```sh
# Generate a key using artifact 2.
nix run ../artifact-leaffaultsim#instancegen -- key1

# Run one supply-chain PoC configuration on that key.
LEAF_VAL=$(nix run .#native-ns -- key1)

# Optionally run the key-recovery simulator.
nix run ../artifact-leaffaultsim#leaffaultsim -- key1 "$LEAF_VAL" 3000000 20000
```

Depending on how artifact 2 exposes its apps, the exact `instancegen` and `leaffaultsim` app names may need to be adjusted. The default app in this artifact already uses the corresponding binaries from the artifact-2 package directly.

## Interpreting the configurations

The intended comparison is:

- `default`: what happens with the default Falcon/compiler behavior;
- `nb`: what changes when `-fno-builtin-sqrt` is added;
- `ns`: what changes when Falcon's handwritten architecture-specific square-root path is bypassed and the generic native `sqrt()` path is forced.

For targets such as x86_64, i686, aarch64, powerpc, and powerpc64le, Falcon contains target-specific square-root code. Merely using `-fno-builtin-sqrt` is not enough to force a libm call in those cases, because the Falcon code may avoid `sqrt()` entirely. The `-ns` configuration is therefore used to force the generic native `sqrt()` path.

For targets without Falcon handwritten square-root code, such as `riscv64` and `loongarch64` in the current matrix, there is no `-ns` configuration. The relevant comparison is between `<target>-default` and `<target>-nb`.

## Key-selection behavior of the PoC

The proof of concept intentionally does **not** attack every Falcon key. The wrapped `sqrt` routine corrupts only a narrow class of square-root inputs. In the current configuration, this means that the induced modified-leaf attack applies to keys whose relevant squared Euclidean length is below:

```text
16384
```

Such keys occur with good probability: the key-length distribution is well approximated by a suitably scaled chi-square distribution, so the threshold selects a non-negligible fraction of keys.

This is not a limitation of the general attack strategy. It is an arbitrary choice made in the design of the PoC wrapper. The point is to demonstrate that an adversary can make the backdoor more selective, and therefore stealthier, by targeting only a subset of keys for which the resulting faulty square-root value lands in an exploitable range. A different trigger or perturbation could target a different subset.

A practical consequence is that, if a generated key does not lead to a successful attack with:

```sh
nix run .#native-ns -- keyN
```

or in the default native smoke test with `RUN_LEAFFAULTSIM=1`, there is generally no point in trying to attack the same key on alternate target platforms. For the purpose of this PoC, that key is simply not in the selected weak-key subset. Generate a new key instead.

## Inspecting the built source

After building any configuration, inspect:

```sh
result/share/falcon-artifact/source/
```

For example:

```sh
nix build .#native-ns
grep -R "FORCE_NATIVESQRT" result/share/falcon-artifact/source/
cat result/share/falcon-artifact/config.txt
```

This lets reviewers verify exactly which patch and compiler flags were used.

## Reproducibility

The artifact includes `flake.lock`, which pins nixpkgs, flake-utils, and the dependency on artifact 2. For artifact review, use:

```sh
nix build --no-update-lock-file
nix run --no-update-lock-file
```

and avoid running:

```sh
nix flake update
```

unless intentionally updating the dependency set.

Some reproducibility caveats remain:

- cross-target builds may take a long time if the cross toolchain is not available from the binary cache;
- QEMU user-mode behavior depends on the host architecture and QEMU version pinned by nixpkgs;
- timing and runtime of `leaffaultsim` depend on the reviewer's CPU;
- `loongarch64` and other less common targets may be more fragile across nixpkgs revisions than mainstream targets.

## Common commands

Build and inspect the default native vulnerable-path model:

```sh
nix build
cat result/share/falcon-artifact/config.txt
ls result/share/falcon-artifact/source/
```

Run the native smoke test without key recovery:

```sh
NUM_KEYS=1 nix run
```

Run the native smoke test with key recovery enabled:

```sh
NUM_KEYS=1 RUN_LEAFFAULTSIM=1 nix run
```

Build and run individual targets:

```sh
nix build .#riscv64-nb
nix run .#riscv64-nb -- key1

nix build .#aarch64-ns
nix run .#aarch64-ns -- key1
```

List all exposed packages and apps:

```sh
nix flake show
```
