{
  description = "Falcon dependency security analysis PoC artifact";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
    artifact-leaffaultsim.url = "path:../artifact-leaffaultsim";
  };

  outputs = { self, nixpkgs, flake-utils, artifact-leaffaultsim }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;

        leaffaultsim = artifact-leaffaultsim.packages.${system}.leaffaultsim;

        # Cross package sets.  We use explicit GNU triples rather than
        # pkgsCross attribute names so that the flake is less sensitive to
        # nixpkgs renamings of convenience cross sets.
        crossPkgs = triple: import nixpkgs {
          localSystem = { inherit system; };
          crossSystem = { config = triple; };
        };

        # Metadata for each target.  The hasFalconAsm flag records whether the
        # Falcon reference code contains architecture-specific handwritten
        # native-sqrt code for that target.  If it does, the supply-chain PoC
        # needs the FORCE_NATIVESQRT patch to force the generic libm sqrt path.
        targetInfo = {
          native = {
            targetLabel = system;
            pkgsFor = pkgs;
            qemuArch = null;
            hasFalconAsm = true;
          };
          i686 = {
            targetLabel = "i686-linux";
            pkgsFor = crossPkgs "i686-unknown-linux-gnu";
            qemuArch = "i386";
            hasFalconAsm = true;
          };
          aarch64 = {
            targetLabel = "aarch64-linux";
            pkgsFor = crossPkgs "aarch64-unknown-linux-gnu";
            qemuArch = "aarch64";
            hasFalconAsm = true;
          };
          powerpc = {
            targetLabel = "powerpc-linux";
            pkgsFor = crossPkgs "powerpc-unknown-linux-gnu";
            qemuArch = "ppc";
            hasFalconAsm = true;
          };
          powerpc64le = {
            targetLabel = "powerpc64le-linux";
            pkgsFor = crossPkgs "powerpc64le-unknown-linux-gnu";
            qemuArch = "ppc64le";
            hasFalconAsm = true;
          };
          riscv64 = {
            targetLabel = "riscv64-linux";
            pkgsFor = crossPkgs "riscv64-unknown-linux-gnu";
            qemuArch = "riscv64";
            hasFalconAsm = false;
          };
          loongarch64 = {
            targetLabel = "loongarch64-linux";
            pkgsFor = crossPkgs "loongarch64-unknown-linux-gnu";
            qemuArch = "loongarch64";
            hasFalconAsm = false;
          };
          # Uncomment if the pinned nixpkgs revision can build the toolchain.
          # m68k = {
          #   targetLabel = "m68k-linux";
          #   pkgsFor = crossPkgs "m68k-unknown-linux-gnu";
          #   qemuArch = "m68k";
          #   hasFalconAsm = false;
          # };
        };

        # Configurations generated from the target metadata.
        #
        #   <target>-default : unmodified Falcon/compiler behavior
        #   <target>-nb      : add -fno-builtin-sqrt
        #   <target>-ns      : force Falcon's generic native sqrt() path and
        #                       add -fno-builtin-sqrt; generated only when
        #                       hasFalconAsm is true
        #
        # We do not generate a forceNativeSqrt-only configuration, because when
        # forceNativeSqrt applies, -fno-builtin-sqrt is also needed to obtain a
        # link-visible call to sqrt on the targets of interest.
        mkConfigsForTarget = target: ti:
          {
            "${target}-default" = {
              inherit target;
              noBuiltinSqrt = false;
              forceNativeSqrt = false;
            };
            "${target}-nb" = {
              inherit target;
              noBuiltinSqrt = true;
              forceNativeSqrt = false;
            };
          }
          // lib.optionalAttrs ti.hasFalconAsm {
            "${target}-ns" = {
              inherit target;
              noBuiltinSqrt = true;
              forceNativeSqrt = true;
            };
          };

        configs = lib.foldl' (acc: target:
          acc // mkConfigsForTarget target targetInfo.${target}
        ) {} (lib.attrNames targetInfo);

        mkSupplysim = name: cfg:
          let
            ti = targetInfo.${cfg.target};
            targetPkgs = ti.pkgsFor;
            cflagsExtra = lib.concatStringsSep " " (
              (lib.optional cfg.noBuiltinSqrt "-fno-builtin-sqrt")
              ++ (lib.optional cfg.forceNativeSqrt "-DFORCE_NATIVESQRT")
            );
          in
          targetPkgs.stdenv.mkDerivation {
            pname = "supplysim-${name}";
            version = "ccs2026artifact";

            src = ./src;

            nativeBuildInputs = [ pkgs.patch ];

            buildPhase = ''
              runHook preBuild

              cp -r ${leaffaultsim}/share/falcon-artifact/source/* .
              chmod -R u+w .

              ${lib.optionalString cfg.forceNativeSqrt ''
              patch fpr.h fpr-optnativesqrt.patch
              ''}

              make -f Makefile.supplysim \
                CC="$CC" \
                CXX="$CXX" \
                LD="$CC" \
                LDXX="$CXX" \
                CFLAGS_EXTRA="${cflagsExtra}" \
                EIGEN3_INCLUDE_DIR=${targetPkgs.eigen_3_4_0}/include/eigen3 \
                TERMCOLOR_INCLUDE_DIR=${targetPkgs.termcolor}/include

              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall

              mkdir -p $out/bin $out/share/falcon-artifact/source
              cp supply_test $out/bin/supply_test
              cp -r . $out/share/falcon-artifact/source

              cat > $out/share/falcon-artifact/config.txt <<EOF_CONFIG
              name=${name}
              target=${cfg.target}
              targetLabel=${ti.targetLabel}
              noBuiltinSqrt=${lib.boolToString cfg.noBuiltinSqrt}
              forceNativeSqrt=${lib.boolToString cfg.forceNativeSqrt}
              hasFalconAsm=${lib.boolToString ti.hasFalconAsm}
              cflagsExtra=${cflagsExtra}
              EOF_CONFIG

              runHook postInstall
            '';
          };

        packageSet = lib.mapAttrs mkSupplysim configs;

        runnerFor = name: cfg:
          let
            ti = targetInfo.${cfg.target};
            pkg = packageSet.${name};
          in
          pkgs.writeShellScriptBin "run-supplysim-${name}" (
            if ti.qemuArch == null then ''
              exec ${pkg}/bin/supply_test "$@"
            '' else ''
              exec ${pkgs.qemu}/bin/qemu-${ti.qemuArch} ${pkg}/bin/supply_test "$@"
            ''
          );

        runnerSet = lib.mapAttrs runnerFor configs;

        appSet = lib.mapAttrs (name: _cfg: {
          type = "app";
          program = "${runnerSet.${name}}/bin/run-supplysim-${name}";
        }) configs;

        # Default smoke-test subset.  This is intentionally native-only so that
        # plain `nix run` does not force expensive cross-toolchain builds.  Use
        # SUPPLYSIM_CONFIGS to select cross targets explicitly.
        defaultDemoConfigs =
          [ "native-default" "native-nb" ]
          ++ lib.optional targetInfo.native.hasFalconAsm "native-ns";

        # Build the default demonstration script only from the selected subset,
        # so that `nix run` does not force all cross-compiled variants to be
        # built.  The full matrix is still available through individual apps.
        demoConfigs = lib.genAttrs defaultDemoConfigs (name: configs.${name});

        caseArms = lib.concatStringsSep "\n" (lib.mapAttrsToList (name: _cfg: ''
          ${name})
            ${runnerSet.${name}}/bin/run-supplysim-${name} "$2"
            ;;
        '') demoConfigs);

        testAttackScript =
          pkgs.writeShellScriptBin "run-falcon-supplysim" ''
            set -eu
            : "''${NUM_KEYS:=5}"
            : "''${MAX_SIGS:=3000000}"
            : "''${PER_CORE_BATCH_SIZE:=20000}"
            : "''${SUPPLYSIM_CONFIGS:=${lib.concatStringsSep " " defaultDemoConfigs}}"
            : "''${RUN_LEAFFAULTSIM:=0}"

            run_supply_test() {
              case "$1" in
            ${caseArms}
                *)
                  echo "Unknown default-script configuration: $1" >&2
                  echo "Configurations available in this default script:" >&2
                  echo "  ${lib.concatStringsSep " " defaultDemoConfigs}" >&2
                  echo "All configurations are available as individual apps; for example:" >&2
                  echo "  nix run .#riscv64-nb -- <keydir>" >&2
                  echo "  nix run .#aarch64-ns -- <keydir>" >&2
                  return 2
                  ;;
              esac
            }

            cat <<EOF_RUN
            Falcon sqrt supply-chain/dependency PoC
            ---------------------------------------
            * Keys to generate: key1 up to key''${NUM_KEYS}
            * Configurations: ''${SUPPLYSIM_CONFIGS}
            * Run leaffaultsim key recovery: ''${RUN_LEAFFAULTSIM}
            * Maximum number of signature samples, if enabled: ''${MAX_SIGS}
            * Signatures processed per core batch, if enabled: ''${PER_CORE_BATCH_SIZE}
            EOF_RUN

            for i in $(seq "''${NUM_KEYS}"); do
              rm -rf "key$i"
              mkdir -p "key$i"
              echo
              echo "=== Generating key$i ==="
              ${leaffaultsim}/bin/instancegen "key$i"

              for cfg in ''${SUPPLYSIM_CONFIGS}; do
                echo
                echo "--- $cfg on key$i ---"
                if LEAF_VAL=$(run_supply_test "$cfg" "key$i"); then
                  echo "modified leaf value: $LEAF_VAL"
                  if [ "''${RUN_LEAFFAULTSIM}" = 1 ] || [ "''${RUN_LEAFFAULTSIM}" = true ] || [ "''${RUN_LEAFFAULTSIM}" = yes ]; then
                    ${leaffaultsim}/bin/leaffaultsim \
                      "key$i" "$LEAF_VAL" \
                      "''${MAX_SIGS}" "''${PER_CORE_BATCH_SIZE}"
                  else
                    echo "key recovery skipped; set RUN_LEAFFAULTSIM=1 to enable it"
                  fi
                else
                  echo "supply_test failed for configuration $cfg" >&2
                fi
              done
            done
          '';

      in {
        packages = packageSet // {
          default = packageSet.native-ns;
        };

        apps = appSet // {
          default = {
            type = "app";
            program = "${testAttackScript}/bin/run-falcon-supplysim";
          };
        };
      });
}
