{
  description = "Falcon ChipWhisperer fault-injection artifact";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
    artifact-leaffaultsim.url = "path:../artifact-leaffaultsim";
  };

  outputs = { self, nixpkgs, flake-utils, artifact-leaffaultsim }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;

        falconReferenceImpl = pkgs.fetchzip {
          url = "https://falcon-sign.info/Falcon-impl-20211101.zip";
          sha256 = "sha256-3jKJv9IT09A6W8hSe7ajlq9CpUhU5IgQgxuapJNPkfk=";
        };

        falconNewAsmImpl = pkgs.fetchFromGitHub {
          owner = "pornin";
          repo = "c-fn-dsa";
          rev = "8ccaee03c587ef73d1dd6bcd652fb2345ddc24c4";
          sha256 = "sha256-jdXOCo6AHEQrpyDYaz/lapNU3qEBZuGXGJCZ0FoIcqc=";
        };

        chipwhisperer = pkgs.callPackage ./chipwhisperer.nix {};

        victimPlatform = "CW308_STM32F4";
        firmwareFile = "glitch_fndsa-${victimPlatform}";

        firmwareConfigs = [
          { 
            name = "falcon2021_cm4asm";
            src = ./firmware/falconimpl_2021;
            falcon = falconReferenceImpl;
            makeOpts = "ENABLE_CM4_ASM=1";
          }
          { 
            name = "falcon2021_c";
            src = ./firmware/falconimpl_2021;
            falcon = falconReferenceImpl;
            makeOpts = "ENABLE_CM4_ASM=0";
          }
          { 
            name = "falcon2025_cm4asm";
            src = ./firmware/falconimpl_cm4_2025;
            falcon = falconNewAsmImpl;
            makeOpts = "";
          }
        ];

        mkFalconGlitchFirmware = cfg:
          pkgs.stdenv.mkDerivation {
            pname = "glitch_${cfg.name}";
            version = "ccs2026artifact";

            src = cfg.src;

            nativeBuildInputs = [
              pkgs.gcc-arm-embedded
            ];

            buildPhase = ''
              runHook preBuild

              cp -r ${cfg.falcon}/* .
              for file in *.s; do mv -- "$file" "''${file/%s/S}"; done
              make -f Makefile.glitch \
                FIRMWAREPATH=${chipwhisperer}/firmware/mcu \
                PLATFORM=${victimPlatform} ${cfg.makeOpts}

              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall

              mkdir -p $out/bin $out/share/falcon-artifact/source

              shopt -s nullglob
              cp ${firmwareFile}.* $out/bin
              cp *.c *.s *.S *.h Makefile* $out/share/falcon-artifact/source
              shopt -u nullglob

              runHook postInstall
            '';
          };

        firmwarePackageList = 
          map (cfg: {
            name = "glitch_${cfg.name}";
            value = mkFalconGlitchFirmware cfg;
          }) firmwareConfigs;

        firmwarePackages = builtins.listToAttrs firmwarePackageList;

        cwPackages = ps: with ps; [ 
          bokeh configobj cycler cython datashader ecpy fastdtw 
          holoviews ipywidgets ipykernel ipympl jupyter 
          jupyter-client libusb1 matplotlib nbconvert notebook 
          numpy pandas pycryptodome pyserial pyyaml 
          terminaltables tqdm
        ];

        pythonEnv = pkgs.python3.withPackages (ps:
          [ chipwhisperer ] ++ cwPackages ps);

        leaffaultsim = artifact-leaffaultsim.packages.${system}.leaffaultsim;

      in {
        packages = firmwarePackages // {
          default = (builtins.head firmwarePackageList).value;
          chipwhisperer = chipwhisperer;
        };

        devShells.default = pkgs.mkShell {
          buildInputs = [ pythonEnv ];

          shellHook = ''
            ASMFNDSA_HEXFILE=${firmwarePackages.glitch_falcon2025_cm4asm}/bin/${firmwareFile}.hex
            ASM21_HEXFILE=${firmwarePackages.glitch_falcon2021_cm4asm}/bin/${firmwareFile}.hex
            C21_HEXFILE=${firmwarePackages.glitch_falcon2021_c}/bin/${firmwareFile}.hex
            PLATFORM=${victimPlatform}
            FAULTSIM_EXE=${leaffaultsim}/bin/leaffaultsim
            NUM_KEYS=10
            export ASMFNDSA_HEXFILE ASM21_HEXFILE C21_HEXFILE PLATFORM
            export FAULTSIM_EXE NUM_KEYS

            ${pkgs.rsync}/bin/rsync -au ${chipwhisperer}/jupyter/ ./cw_jupyter/

            cat <<EOF
            This development shell contains all the necessary dependencies to run the
            fault attack on the target microcontroller (''${PLATFORM}) using the
            CW1200 ChipWhisperer Pro, and then perform key recovery on the resulting
            faulty keys. If you have the ChipWhisperer and the target board, you can
            simply run:

              python3 scripts/full_chain.py

            to automatically generate key pairs, perform the glitch attack against
            them via ChipWhisperer, and then mount the key recovery. The code will
            attack each of the 3 target implementations (ASM21, C21, ASM25) one by
            one, using NUM_KEYS for each.

            You can also run:

              jupyter lab

            to launch a local Jupyter server, browse the code and test it in a
            notebook. The notebooks (documentation and tutorials) that come with the
            ChipWhisperer code are also automatically copied to the ./cw_jupyter
            repository, so they can be viewed conveniently in Jupyter.


            Local environment
            =================
            ASMFNDSA_HEXFILE = ''${ASMFNDSA_HEXFILE}
            ASM21_HEXFILE    = ''${ASM21_HEXFILE}
            C21_HEXFILE      = ''${C21_HEXFILE}
            PLATFORM         = ''${PLATFORM}
            FAULTSIM_EXE     = ''${FAULTSIM_EXE}
            NUM_KEYS         = ''${NUM_KEYS}

            EOF
          '';
        };
      });
}
