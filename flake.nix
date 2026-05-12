{
  description = "blur devshell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
  }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs {inherit system;};
    in {
      devShells.default = with pkgs;
        mkShell {
          packages = [
            git
            clang
            clang-tools
            ninja
            cmake
            vcpkg
            vcpkg-tool
            pkg-config
            uv
            meson
          ];

          shellHook = ''
            export VCPKG_ROOT=${pkgs.vcpkg}/share/vcpkg
          '';
        };
    });
}
