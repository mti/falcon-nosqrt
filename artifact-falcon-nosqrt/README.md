# Falcon square-root-free implementation artifact

This artifact contains the square-root-free implementation of Falcon used in the paper. More precisely, it contains:

- a patch, `patches/nosqrt.patch`, against the Falcon reference implementation;
- a Nix flake that fetches the pristine Falcon reference implementation (`Falcon-impl-20211101.zip`), optionally applies the patch, and builds the resulting code under several floating-point configurations;
- Nix app targets for running Falcon's `speed` benchmark and `test_falcon` test binary on each configuration.

The artifact is intended to make it easy to compare the original Falcon implementation with the square-root-free variant, while keeping the upstream Falcon code separate from the changes introduced by the paper.

## Requirements

Install Nix with flake support enabled. For example, recent Nix installations can run the commands below directly with:

```sh
nix build
nix run
```

On older installations, you may need to enable flakes explicitly:

```sh
nix --extra-experimental-features 'nix-command flakes' build
nix --extra-experimental-features 'nix-command flakes' run
```

The flake currently uses `nixpkgs` from `github:NixOS/nixpkgs/nixos-25.11` and fetches the Falcon reference implementation from the official Falcon website with a pinned hash.

## Inspecting the patched source

The default package is the square-root-free implementation in floating-point emulation mode (`nosqrt-fpemu`). Build it with:

```sh
nix build
```

The patched source tree is installed under:

```sh
result/share/falcon-artifact/source/
```

For example:

```sh
ls result/share/falcon-artifact/source/
```

This directory contains the Falcon source after applying `patches/nosqrt.patch`. It is useful for reviewing the actual code that was compiled by Nix.

To inspect a different configuration, build it explicitly, for example:

```sh
nix build .#baseline-fpnative
nix build .#nosqrt-avx2
```

and then inspect the same `result/share/falcon-artifact/source/` directory.

## Build configurations

The flake builds two implementation variants:

- `baseline`: the original Falcon reference implementation;
- `nosqrt`: the square-root-free implementation obtained by applying `patches/nosqrt.patch` and compiling with `-DNOSQRT`.

Each variant is built with several floating-point arithmetic configurations:

- `fpemu`: Falcon's integer-based floating-point emulation (`FALCON_FPEMU=1`, `FALCON_FPNATIVE=0`, `FALCON_AVX2=0`);
- `fpnative`: platform-native floating-point arithmetic (`FALCON_FPEMU=0`, `FALCON_FPNATIVE=1`, `FALCON_AVX2=0`);
- `avx2`: native floating point with Falcon's AVX2 code paths enabled (`FALCON_FPEMU=0`, `FALCON_FPNATIVE=1`, `FALCON_AVX2=1`).

The AVX2 configurations are exposed only on `x86_64` systems by the flake.

The package names are:

```text
baseline-fpemu
nosqrt-fpemu
baseline-fpnative
nosqrt-fpnative
baseline-avx2       # x86_64 only
nosqrt-avx2         # x86_64 only
```

Each package installs two binaries:

```text
bin/speed
bin/test_falcon
```

## Running the benchmark matrix

To run Falcon's `speed` benchmark for all available configurations, use:

```sh
nix run
```

This runs the benchmark matrix and prints the relevant timing lines for each configuration. The output labels are Falcon's usual labels:

```text
kg  = key generation
ek  = expand private key
sd  = sign without expanded key
st  = sign with expanded key
vv  = verify
sdc, stc, vvc = corresponding constant-time hash-to-point variants
```

Key generation is reported in milliseconds; the other values are reported in microseconds, following Falcon's `speed` tool.

## Running individual configurations

The flake also exposes app targets for each binary/configuration pair. The naming convention is:

```text
.#<binary>-<variant>-<fpmode>
```

where `<binary>` is either `speed` or `test_falcon`, `<variant>` is `baseline` or `nosqrt`, and `<fpmode>` is `fpemu`, `fpnative`, or `avx2` where available.

Examples:

```sh
nix run .#speed-baseline-fpnative
nix run .#speed-nosqrt-fpnative
nix run .#speed-baseline-fpemu
nix run .#speed-nosqrt-fpemu
```

On `x86_64` systems, AVX2 targets are also available:

```sh
nix run .#speed-baseline-avx2
nix run .#speed-nosqrt-avx2
```

The same convention applies to Falcon's `test_falcon` binary:

```sh
nix run .#test_falcon-baseline-fpnative
nix run .#test_falcon-nosqrt-fpemu
```

For example, to run the reference implementation test suite in floating-point emulation mode and then the square-root-free version in the same mode:

```sh
nix run .#test_falcon-baseline-fpemu
nix run .#test_falcon-nosqrt-fpemu
```

## Building without running

To build any configuration without executing it, use `nix build` with the package name:

```sh
nix build .#baseline-fpemu
nix build .#nosqrt-fpemu
nix build .#baseline-fpnative
nix build .#nosqrt-fpnative
```

On `x86_64` systems:

```sh
nix build .#baseline-avx2
nix build .#nosqrt-avx2
```

The resulting binaries are available under:

```text
result/bin/speed
result/bin/test_falcon
```

and the compiled source tree is available under:

```text
result/share/falcon-artifact/source/
```

## Notes on reproducibility

The Nix derivations fix the upstream Falcon source archive and the compiler flags used for each configuration. The benchmark results themselves still depend on the host CPU, microarchitecture, frequency scaling, thermal state, operating-system scheduling, and whether the CPU supports AVX2. For stable timing measurements, it is recommended to run on an otherwise idle machine and, where possible, pin the process to a fixed CPU core and disable aggressive frequency scaling.

