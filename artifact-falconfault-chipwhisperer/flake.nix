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

        falconNewAsmImpl = pkgs.fetchFromGitHub {
          owner = "pornin";
          repo = "c-fn-dsa";
          rev = "8ccaee03c587ef73d1dd6bcd652fb2345ddc24c4";
          sha256 = "sha256-jdXOCo6AHEQrpyDYaz/lapNU3qEBZuGXGJCZ0FoIcqc=";
        };

        chipwhisperer = pkgs.callPackage ./chipwhisperer.nix {};

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
              make -f Makefile.glitch FIRMWAREPATH=${chipwhisperer}/firmware/mcu ${cfg.makeOpts}

              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall

              mkdir -p $out/bin $out/share/falcon-artifact/source

              shopt -s nullglob
              cp glitch_fndsa-* $out/bin
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

      in {
        packages = firmwarePackages // {
          default = (builtins.head firmwarePackageList).value;
          chipwhisperer = chipwhisperer;
        };
      });
}
