# KVM Sectorlisp

Zero dependency virtualization of [sectorlisp](https://github.com/jart/sectorlisp) using KVM.
For further information, see [my mastodon](https://hachyderm.io/@bolun/113189699526325226) post.

Requirements:
- Linux 2.6.20+ built wtih virtualization enabled.
- x86 CPU with hardware virtualization support.
- To compile: clang 19+ (to support C23's `#embed` feature)

Great thanks to the [LWN post on the KVM API](https://lwn.net/Articles/658511/) for acting as a guide.

## Building
```
git clone --recurse-submodules
make
```
Clang 19 is the only dependency (for C23 support).

### NixOS Notes

To build for NixOS, run `nix build '.?submodules=1'`. See [discourse for why](https://discourse.nixos.org/t/get-nix-flake-to-include-git-submodule/30324/16).
