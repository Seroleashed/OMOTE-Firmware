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
            export PLATFORMIO_CORE_DIR="$PWD/.platformio"
            # PlatformIO ships pre-compiled binaries (xtensa toolchain) that
            # expect a normal FHS system. nix-ld makes them work on NixOS.
            export NIX_LD="$(cat ${pkgs.stdenv.cc}/nix-support/dynamic-linker)"
            export NIX_LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [
              pkgs.stdenv.cc.cc
              pkgs.zlib
              pkgs.libusb1
              pkgs.ncurses5
            ]}"
            echo "OMOTE dev shell"
            echo "  pio test -e native_test     # unit tests, no hardware needed"
            echo "  pio run  -e linux_64bit     # LVGL simulator (SDL2 window)"
            echo "  pio run  -e esp32-s3-Rev5andHigher   # firmware build"
          '';
        };
      });
}
