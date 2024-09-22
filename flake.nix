{
  description = "kvm sectorlisp";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      devShells.x86_64-linux.default = pkgs.mkShell {
          name = "KVM Sectorlisp Shell";
          nativeBuildInputs = with pkgs; with llvmPackages_19; [
            clang-tools
            clang
          ];
          packages = with pkgs; with llvmPackages_19; [
            gdb
            binutils
            blink
            git
          ];
        };
    };
}
