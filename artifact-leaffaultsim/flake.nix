{
  description = "Falcon modified-leaf statistical key-recovery artifact";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;

        falconReferenceImpl = pkgs.fetchzip {
          url = "https://falcon-sign.info/Falcon-impl-20211101.zip";
          sha256 = "sha256-3jKJv9IT09A6W8hSe7ajlq9CpUhU5IgQgxuapJNPkfk=";
        };

        leaffaultsim = pkgs.stdenv.mkDerivation {
          pname = "leaffaultsim";
          version = "artifact";

          src = ./src;

          buildInputs = [
            pkgs.eigen_3_4_0
          ];

          buildPhase = ''
            runHook preBuild

            cp -r ${falconReferenceImpl}/* .
            cp vrfy.c vrfy_mq.h
            patch vrfy_mq.h vrfy_mq.patch
            make -f Makefile.leaffaultsim \
              EIGEN3_INCLUDE_DIR=${pkgs.eigen_3_4_0}/include/eigen3 \
              TERMCOLOR_INCLUDE_DIR=${pkgs.termcolor}/include

            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall

            mkdir -p $out/bin
            mkdir -p $out/bin $out/share/falcon-artifact/source

            cp leaffaultsim $out/bin
            cp instancegen $out/bin
            cp *.c *.cpp *.h Makefile* $out/share/falcon-artifact/source

            runHook postInstall
          '';
        };

        testAttackScript =
          pkgs.writeShellScriptBin "run-falcon-leaffaultsim" ''
            set -eu
            : "''${NUM_KEYS:=5}"
            : "''${LEAF_VAL:=0.55}"
            : "''${MAX_SIGS:=3000000}"
            : "''${PER_CORE_BATCH_SIZE:=10000}"
            cat <<EOF
            Simulation of the key recovery attack against Falcon with a faulty leaf
            -----------------------------------------------------------------------
            * Keys to attack: key1 up to key''${NUM_KEYS}
              (existing key files will be used if they exist; otherwise, they are
               freshly generated);
            * Injected leaf value: ''${LEAF_VAL};
            * Maximum number of signature samples: ''${MAX_SIGS};
            * Number of signatures processed at once on each core: ''${PER_CORE_BATCH_SIZE}
            EOF
            

            for i in $(seq ''${NUM_KEYS}); do
              mkdir -p key$i
              echo 
              echo === Attacking key$i ===
              ${leaffaultsim}/bin/leaffaultsim key$i ''${LEAF_VAL} ''${MAX_SIGS} ''${PER_CORE_BATCH_SIZE}
            done
            '';

      in {
        packages = {
          default = leaffaultsim;
          leaffaultsim = leaffaultsim;
        };

        apps = {
          default = {
            type = "app";
            program = "${testAttackScript}/bin/run-falcon-leaffaultsim";
          };

          leaffaultsim = {
            type = "app";
            program = "${leaffaultsim}/bin/leaffaultsim";
          };
        };
      });
}
