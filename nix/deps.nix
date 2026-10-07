# The libraries the project, its tests and its benchmarks are built against.
#
# This is the one place they are chosen. Bazel takes them from here (through
# bazel.nix) at the versions flake.lock pins.
pkgs:
let
  stdenv = pkgs.llvmPackages_21.stdenv;
  abseil-cpp = pkgs.abseil-cpp.override { cxxStandard = "20"; };

  # Bedrock lives in a repository of its own. It is an input of the flake,
  # so flake.lock pins a revision of it, and this fetches that revision.
  lock = builtins.fromJSON (builtins.readFile ../flake.lock);
  bedrock-source =
    let
      locked = lock.nodes.${lock.nodes.root.inputs.bedrock}.locked;
    in
    builtins.fetchTarball {
      url = "https://github.com/${locked.owner}/${locked.repo}/archive/${locked.rev}.tar.gz";
      sha256 = locked.narHash;
    };
in
{
  # Built as C++20 like the project, so that the two agree on which standard
  # library types Abseil's own types stand for.
  inherit abseil-cpp;

  # What bedrock is built against. Nothing here uses them directly.
  liburing = pkgs.liburing;
  openssl = pkgs.openssl;
  sqlite = pkgs.sqlite;

  # Coroutines, the event loop, child processes as streams, and JSON. Built
  # here by the package.nix it carries, with this project's compiler and
  # against the libraries above, so that the whole program is built one way.
  bedrock = pkgs.callPackage "${bedrock-source}/nix/package.nix" {
    inherit stdenv abseil-cpp;
    liburing = pkgs.liburing;
    openssl = pkgs.openssl;
    sqlite = pkgs.sqlite;
    gtest = pkgs.gtest;
    gbenchmark = pkgs.gbenchmark;
  };

  # For tests and benchmarks only.
  gtest = pkgs.gtest;
  gbenchmark = pkgs.gbenchmark;
}
