{
  description = "MLJIT development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          llvm = pkgs.llvmPackages_22;
        in
        {
          default =
            (pkgs.mkShell.override {
              stdenv = pkgs.overrideCC pkgs.gcc16Stdenv llvm.clang;
            })
              {
                packages = [
                  pkgs.cmake
                  pkgs.ninja
                  pkgs.antlr4_13
                  pkgs.antlr4_13.runtime.cpp
                  pkgs.jre
                  pkgs.catch2_3
                  pkgs.pkg-config
                  pkgs.git
                  llvm.clang-tools
                ];

                shellHook = ''
                  export CC=clang
                  export CXX=clang++
                  echo "MLJIT dev shell: $(clang++ --version | head -n 1)"
                '';
              };
        }
      );
    };
}
