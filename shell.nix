{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
	nativeBuildInputs = [
		pkgs.clang
		pkgs.libarchive
		pkgs.lld
		pkgs.meson
		pkgs.ninja
		pkgs.sqlite
	];
}
