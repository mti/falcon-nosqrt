# Falcon modified-leaf key-recovery artifact

This artifact contains the code used to simulate the statistical key-recovery attack against Falcon when one leaf of the Falcon tree is modified.  Given a Falcon key and a chosen faulty leaf value, the program generates signatures from the corresponding faulty signing distribution, applies the PCA-based recovery strategy described in the paper, and attempts to recover the secret key.

The artifact is packaged as a Nix flake.  The flake downloads the reference Falcon implementation, applies the small patch needed by the simulator, builds the C++ key-recovery code together with the Falcon code, and supplies Eigen through `nixpkgs` rather than vendoring it in the artifact.

## Requirements

You need Nix with flake support enabled.  For example:

```sh
nix --version
```

The first build may download `nixpkgs`, the Falcon reference implementation, and build dependencies.  Subsequent builds should be cached by Nix in the usual way.

## Building

From the root of the artifact directory, run:

```sh
nix build
```

This builds the default package, `leaffaultsim`.  The resulting executable is available as:

```sh
./result/bin/leaffaultsim
```

The same package can also be selected explicitly:

```sh
nix build .#leaffaultsim
```

## Running the simulator directly

The simulator is exposed as a Nix app:

```sh
nix run .#leaffaultsim -- <key-directory> <faulty-leaf-value> <max-signatures> <per-core-batch-size>
```

For example:

```sh
nix run .#leaffaultsim -- key1 0.55 3000000 10000
```

If the key directory already contains a generated key, the simulator reuses it.  Otherwise, the simulator generates the necessary key material in that directory.  The remaining arguments specify the injected leaf value, the maximum number of generated signatures, and the number of signatures processed at once on each core.

## Convenience attack script

The default app runs a small batch experiment over several keys:

```sh
nix run
```

By default, this attacks keys `key1` through `key5`, using:

```text
NUM_KEYS=5
LEAF_VAL=0.55
MAX_SIGS=3000000
PER_CORE_BATCH_SIZE=10000
```

These parameters can be overridden through environment variables:

```sh
NUM_KEYS=20 LEAF_VAL=0 MAX_SIGS=5000000 PER_CORE_BATCH_SIZE=20000 nix run
```

The script creates directories named `key1`, `key2`, and so on in the current working directory.  Existing key directories are reused, which makes it possible to resume or repeat experiments on the same keys.

## Flake outputs

The flake provides the following package outputs:

```text
packages.<system>.default
packages.<system>.leaffaultsim
```

and the following app outputs:

```text
apps.<system>.default
apps.<system>.leaffaultsim
```

Thus the most common commands are:

```sh
nix build
nix build .#leaffaultsim
nix run
nix run .#leaffaultsim -- key1 0.55 3000000 10000
```

## Reproducibility notes

Note that even for a fixed key file, two executions of the attack may require a different number of signatures to succeed. That is because the signature samples, as in a real attack setting, are randomized (at least due to their varying embedded salt values). Nevertheless, you will certainly observe that for the example leaf value of 0.55, successful key recovery happens well before a million signatures most of the time. A larger number of signatures is likely needed for faulty leaf values very close to the original leaf value.
