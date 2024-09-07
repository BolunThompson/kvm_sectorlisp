{
  description = "Yamml Flake";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      allSystems = [
        "x86_64-linux" # 64-bit Intel/AMD Linux
        # no more for now
      ];

      forAllSystems = f: nixpkgs.lib.genAttrs allSystems (system: f {
        pkgs = import nixpkgs { inherit system; config.allowUnfree = true; };
      });
    in
    {
      devShells = forAllSystems ({ pkgs }: {
        default = pkgs.mkShellNoCC {
          name = "KVM Sectorlisp Shell";
          # TODO: Which of these do i actually need?
          nativeBuildInputs = with pkgs; [
            gnat # with gcc (also for coreboot)
            # to bootstrap coreboot
            pkg-config
            ncurses
            bison
            curl
            flex
            git
            gnat
            openssl
            m4
            zlib
          ];
          packages = with pkgs; [
            clang-tools
            binutils
            blink
          ];
        };
      }
      );
    };
}
