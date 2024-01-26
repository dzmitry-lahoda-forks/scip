{
  description = "SCIP Optimization Suite";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    soplex-src = {
      url = "github:scipopt/soplex";
      flake = false;
    };
    papilo-src = {
      url = "github:scipopt/papilo";
      flake = false;
    };
    # just to get zimpl, would be awesome zimpl be just git repo in future:)
    suite-src = {
      url = "https://scipopt.org/download/release/scipoptsuite-8.0.3.tgz";
      flake = false;
    };
    ipopt = {
      url = "github:dzmitry-lahoda-forks/Ipopt/3eabbd888a6c5b83448083876722296ce20039c3";
    };
    mumps = {
      url = "github:dzmitry-lahoda-forks/mumps/1b56c9295f8cf23ddb20400c44255fa984526977";
    };
  };

  outputs = inputs@{ flake-parts, suite-src, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" "x86_64-darwin" ];
      perSystem = { config, self', inputs', pkgs, system, ... }: {
        packages =
          let
            # need cmake support with binayr libs be same folder as dev texts of very specific version
            tbb-all-cmake = pkgs.tbb_2020_3.overrideAttrs (old: rec {
              doCheck = false;
              name = "tbb-all-cmake";
              outputs = [ "out" ];
              installPhase = old.installPhase + ''            
                mkdir --parents "$out"/lib/cmake/TBB
                ${pkgs.cmake}/bin/cmake \
                    -DINSTALL_DIR="$out"/lib/cmake/TBB \
                    -DSYSTEM_NAME=Linux -DTBB_VERSION_FILE="$out"/include/tbb/tbb_stddef.h \
                    -P cmake/tbb_config_installer.cmake
                cp --recursive --force ${pkgs.tbb_2020_3.dev}/include "$out"
                cp --recursive --force ${pkgs.tbb_2020_3.dev}/lib/pkgconfig "$out"/lib
                cp --recursive --force ${pkgs.tbb_2020_3.dev}/nix-support "$out"
              '';
            });
            scip-src = pkgs.nix-gitignore.gitignoreSource [ ] ./.;
            # all is here, really need to fine tune per package
            build-inputs = [
              pkgs.boost.dev
              pkgs.cmake
              pkgs.gmp.dev
              pkgs.pkg-config
              pkgs.gfortran
              pkgs.blas
              pkgs.bison
              pkgs.flex
              pkgs.libamplsolver
            ];
            cmake-gmp = [
              "-D GMP=on"
              "-D STATIC_GMP=on"
            ];
          in
          rec {
            soplex = pkgs.stdenv.mkDerivation {
              name = "soplex";
              src = inputs.soplex-src;
              buildInputs = build-inputs;
              nativeBuildInputs = build-inputs;
              doCheck = false;
              cmakeFlags =
                [ "-D PAPILO=off" ] ++
                cmake-gmp;
            };
            papilo = pkgs.stdenv.mkDerivation {
              name = "papilo";
              src = inputs.papilo-src;
              nativeBuildInputs = build-inputs;
              buildInputs = build-inputs ++ [ tbb-all-cmake soplex ];
              cmakeFlags = [
                "-D GMP=on"
              ];
              doCheck = false;
            };
            zimpl = pkgs.stdenv.mkDerivation {
              name = "zimpl";
              src = "${suite-src}/zimpl";
              nativeBuildInputs = build-inputs;
              buildInputs = build-inputs;
              doCheck = false;
            };
            default = scip;
            inherit tbb-all-cmake;
            scip = scip-ipopt-mumps-seq-papilo-soplex;
            scip-ipopt-mumps-seq-papilo-soplex =
              pkgs.stdenv.mkDerivation {
                name = "scip";
                src = scip-src;
                LD_LIBRARY_PATH = with pkgs; lib.strings.makeLibraryPath [
                  "${inputs'.mumps.packages.mumps-32-seq}/lib"
                ];

                cmakeFlags = [
                  "-D AMPL=on"
                  "-D AUTOBUILD=off" # by no means in nix can use internet and scan outer host system
                  "-D CMAKE_BUILD_TYPE=Release"
                  "-D SHARED=on"
                  "-D CMAKE_CXX_COMPILER_ID=GNU"
                  "-D COVERAGE=off"
                  "-D DEBUGSOL=on"
                  "-D IPOPT=on"
                  "-D LPS=spx" # soplex
                  "-D LPSCHECK=off"
                  "-D PAPILO=on"
                  "-D READLINE=on"
                  "-D TBB_DIR=${tbb-all-cmake}"
                  "-D THREADSAFE=on"
                  "-D WORHP=off"
                  "-D ZIMPL_DIR=${zimpl}"
                  "-D ZLIB=on"
                ] ++ cmake-gmp;
                dontFixCmake = true;
                dontUseCmakeConfigure = false;
                nativeBuildInputs = build-inputs ++ [ papilo soplex tbb-all-cmake pkgs.criterion ];
                buildInputs = build-inputs ++ [
                  pkgs.bliss
                  pkgs.readline.dev
                  pkgs.zlib.dev
                  papilo
                  soplex
                  inputs'.ipopt.packages.ipopt-mumps-seq
                  inputs'.mumps.packages.mumps-32-seq
                  tbb-all-cmake
                  pkgs.criterion.dev
                ];

                doCheck = false;

                postInstall = ''                
                  cp --dereference --no-preserve=mode,ownership --recursive --force $out/lib/libscip.so $out/lib/scip.so
                  mkdir --parents $out/lib/shared
                  cp --dereference --no-preserve=mode,ownership --recursive --force $out/lib/libscip.so $out/lib/shared/libscip.so

                  cp --dereference --no-preserve=mode,ownership --recursive --force $out/include/scip $out/include/nlpi
                '';
              };
          };
      };
    };
}
