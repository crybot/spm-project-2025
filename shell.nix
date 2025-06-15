{ pkgs ? import <nixpkgs> {} }:

let
  fastflow-lib = pkgs.stdenv.mkDerivation rec {
    name = "fastflow";

    buildInputs = [
      pkgs.hwloc
    ];

    src = pkgs.fetchFromGitHub {
      owner = "fastflow";
      repo = "fastflow";
      rev = "eec006fb1af5c690bdacecf889e95aa25c5c7419"; # Latest commit hash on `master` branch: 2024-04-24
      hash = "sha256-8D9G5g27D7SkzG3hMFNQuFWs4JgUNmajvtkuaeFEW58=";
    };

    # FastFlow mentions running this script.
    # It seems to modify/generate files within the ff directory.
    buildPhase = ''
      runHook preBuild
      cd $src/ff
      bash ./mapping_string.sh # interpreter diretive uses !/bin/bash which is not available on nixos
      runHook postBuild
      '';

    installPhase = ''
      runHook preInstall
      # Install the ff directory which contains all headers
      mkdir -p $out/include
      cp -r $src/ff $out/include/
      runHook postInstall
      '';

    meta = with pkgs.lib; {
      description = "A C++ parallel programming framework for multi-core architectures";
      homepage = "https://github.com/fastflow/fastflow";
      license = licenses.gpl2Plus; 
      platforms = platforms.all;
    };
  };
in
  pkgs.mkShell {
    name = "fastflow-dev-shell";

    buildInputs = [
      fastflow-lib
      pkgs.gcc
      pkgs.gnumake
      pkgs.hwloc
      pkgs.meson # Does not support c++20 modules
      # pkgs.cmake
      pkgs.ninja
      pkgs.pkg-config
    ];

    # shellHook = ''
    #   echo "FastFlow headers are available in ${fastflow-lib}/include"
    #   export CPLUS_INCLUDE_PATH="${fastflow-lib}/include:$CPLUS_INCLUDE_PATH"
    # '';
    # Adding fastflow-lib to buildInputs should make compilers find it automatically via mechanisms like pkg-config or
    # by inspecting the include paths of dependencies. For header-only libraries, CPLUS_INCLUDE_PATH or similar compiler
    # flags are often set. Nix's stdenv setup hooks for buildInputs usually handle this for C/C++ projects.
  }
