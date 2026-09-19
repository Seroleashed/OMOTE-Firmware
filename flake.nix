{
  description = "OMOTE firmware development environment (PlatformIO, LVGL simulator, native unit tests)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            # --- PlatformIO / ESP32 toolchain -------------------------------
            platformio-core
            esptool
            # PlatformIO downloads pre-built toolchains that are not patched
            # for NixOS. We therefore let it run them through nix-ld / FHS,
            # see shellHook below.

            # --- native build + LVGL simulator ------------------------------
            gcc
            gdb
            gnumake
            cmake
            pkg-config
            SDL2
            SDL2_image

            # --- unit tests / analysis --------------------------------------
            clang-tools # clangd for VS Code, clang-format
            cppcheck

            # --- helper tooling (phase 1/2: config tooling, phase 4: web UI) --
            uv
            python3
            nodejs_22
            pnpm
            jq
          ];

          shellHook = ''
            # PLATFORMIO_CORE_DIR is deliberately left alone, so PlatformIO uses
            # its default ~/.platformio: the xtensa toolchain is roughly 1-2 GB
            # and is shared with every other PlatformIO project instead of being
            # downloaded again per checkout. Set it here if you ever need a
            # toolchain pinned to this repository.
            # PlatformIO ships pre-compiled binaries (xtensa toolchain) that
            # expect a normal FHS system. nix-ld makes them work on NixOS.
            export NIX_LD="$(cat ${pkgs.stdenv.cc}/nix-support/dynamic-linker)"
            export NIX_LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [
              pkgs.stdenv.cc.cc
              pkgs.zlib
              pkgs.libusb1
              pkgs.ncurses5
            ]}"
            # The SDL2 simulator: platformio.ini includes <SDL2/SDL.h>, which the
            # nix compiler wrapper finds. SDL_image.h however does a plain
            # #include "SDL.h", and for that the .../include/SDL2 directory has
            # to be on the include path too - the wrapper only adds
            # .../include. pkg-config knows both, so ask it instead of hard
            # coding store paths.
            export NIX_CFLAGS_COMPILE="$(pkg-config --cflags-only-I sdl2 SDL2_image) $NIX_CFLAGS_COMPILE"

            echo "OMOTE dev shell"
            echo "  pio test -e native_test     # unit tests, no hardware needed"
            echo "  pio run  -e linux_64bit     # LVGL simulator (SDL2 window)"
            echo "  pio run  -e esp32-s3-Rev5andHigher   # firmware build"
          '';
        };
      });
}
