{
  description = "Falcon sqrt-free implementation artifact";

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

        configs = [
          { nosqrt = false; fpMode = "fpemu";    }
          { nosqrt = true;  fpMode = "fpemu";    }
          { nosqrt = false; fpMode = "fpnative"; }
          { nosqrt = true;  fpMode = "fpnative"; }
        ] ++ lib.optionals pkgs.stdenv.hostPlatform.isx86_64 [
          { nosqrt = false; fpMode = "avx2";     }
          { nosqrt = true;  fpMode = "avx2";     }
        ];
        
        configName = { nosqrt, fpMode }:
          if nosqrt then "nosqrt-${fpMode}" else "baseline-${fpMode}";

        mkFalconPkg = { nosqrt, fpMode }@cfg:
          let name = configName cfg; in
          pkgs.stdenv.mkDerivation {
            inherit name;
            pname = "falcon-speed-${name}";
            version = "ccs2026artifact";

            src = falconReferenceImpl;

            nativeBuildInputs = with pkgs; [ 
              gcc gnumake
            ];

            patches = lib.optionals nosqrt [
              ./patches/nosqrt.patch
            ];

            configurePhase = ":";

            buildPhase =
              let
                falconDefaultCFlags =
                  "-Wall -Wextra -Wshadow -Wundef -O3 ";
                defs =
                  (if nosqrt then "-DNOSQRT " else "") +
                  (if fpMode == "fpemu" then
                    "-DFALCON_FPEMU=1 -DFALCON_FPNATIVE=0 -DFALCON_AVX2=0 "
                  else if fpMode == "fpnative" then
                    "-DFALCON_FPEMU=0 -DFALCON_FPNATIVE=1 -DFALCON_AVX2=0 "
                  else if fpMode == "avx2" then
                    "-DFALCON_FPEMU=0 -DFALCON_FPNATIVE=1 -DFALCON_AVX2=1 "
                  else
                    throw "unknown fpMode");
              in ''
                make clean || true
                make CC=gcc LD=gcc CFLAGS="${falconDefaultCFlags + defs}"
              '';

            installPhase = ''
              mkdir -p $out/bin $out/share/falcon-artifact/source
              cp speed test_falcon $out/bin
              cp *.c *.h Makefile $out/share/falcon-artifact/source
            '';

            meta = {
              description = "Falcon package (${name})";
            };
          };

        packageList = mkName: mkValue:
          map (cfg: {
            name = mkName cfg;
            value = mkValue cfg;
          }) configs;

        packages = builtins.listToAttrs (packageList configName mkFalconPkg);

        mkRunApp = binName: cfg:
          let pkgName = configName cfg; in {
            type = "app";
            program = "${pkgs.writeShellScriptBin "run-${pkgName}" ''
              exec ${packages.${pkgName}}/bin/${binName} "$@"
            ''}/bin/run-${pkgName}";
          };

        appList = binName:
          packageList (cfg: "${binName}-${configName cfg}") (mkRunApp binName);

        apps = (builtins.listToAttrs (appList "speed"      )) //
               (builtins.listToAttrs (appList "test_falcon"));

        runAllScript =
            pkgs.writeShellScriptBin "run-all-falcon-speed" (
              ''
              set -eu
              ${pkgs.coreutils}/bin/cat <<EOF
              kg = keygen, ek = expand private key, sd = sign (without expanded key)
              st = sign (with expanded key), vv = verify
              sdc, stc, vvc: like sd, st and vv, but with constant-time hash-to-point
              keygen in milliseconds, other values in microseconds
              EOF
              ''
              +
              lib.concatMapStringsSep "\n" (cfg:
                let
                  falconPkg = packages.${configName cfg};
                in
                ''
                echo
                echo "=== ${configName cfg} ==="
                ${falconPkg}/bin/speed | ${pkgs.coreutils}/bin/tail +7
                ''
              ) configs
            );
      in {
        packages = packages // {
          default = packages.nosqrt-fpemu;
        };
        
        apps = apps // {
          default = {
            type = "app";
            program = "${runAllScript}/bin/run-all-falcon-speed";
          };
        };
      });
}
