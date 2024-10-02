{
  description = "kvm sectorlisp";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
      stdenv = pkgs.llvmPackages_19.stdenv;
    in
    {
      devShells.x86_64-linux.default = pkgs.mkShell.override {inherit stdenv;} {
        name = "KVM Sectorlisp Shell";
        packages = with pkgs; with llvmPackages_19; [
          gdb
          blink
          binutils
          git
          clang-tools
        ];
      };
      defaultPackage.x86_64-linux = stdenv.mkDerivation {
        name = "KVM Sectorlisp";
        version = "1.0.0";
        src = ./.;
        installPhase = ''
          mkdir -p $out/bin
          mv kvm_sectorlisp $out/bin
        '';
      };
    };
}
