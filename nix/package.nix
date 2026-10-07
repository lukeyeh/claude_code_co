# claude_code_co as a Nix package: one static library, its headers, and a
# BUILD file that presents them to Bazel.
#
# This is how other projects use it. They call this file with their own
# compiler and their own bedrock, so that everything in one program is built
# the same way.
#
# It compiles the same sources as `bazel build //...` does in development.
# What the two builds must agree on is the compiler flags, below and in
# .bazelrc. Source files need no upkeep: every non-test .cc file is compiled.
{
  lib,
  stdenv,
  bedrock,
}:

stdenv.mkDerivation {
  pname = "claude-code-co";
  version = "0.1.0";

  # Only the sources, so that editing a README or a BUILD file does not
  # rebuild the package.
  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ./installed.BUILD
      ../cli
      ../protocol
      ../session
    ];
  };

  # Propagated, because the headers installed here include bedrock's, which
  # brings Abseil with it.
  propagatedBuildInputs = [ bedrock ];

  buildPhase = ''
    runHook preBuild

    mkdir objects
    for source in $(find cli protocol session -name '*.cc' ! -name '*_test.cc'); do
      # The flags mirror .bazelrc: C++20, no exceptions.
      $CXX -std=c++20 -O2 -fno-exceptions \
        -Wno-coroutine-missing-unhandled-exception \
        -I. -c "$source" -o "objects/$(basename "$source" .cc).o"
    done
    $AR rcs libclaude_code.a objects/*.o

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    install -Dm644 libclaude_code.a $out/lib/libclaude_code.a
    for header in cli/*.h protocol/*.h session/*.h; do
      install -Dm644 "$header" "$out/include/$header"
    done
    install -Dm644 nix/installed.BUILD $out/BUILD.bazel

    runHook postInstall
  '';

  meta = {
    description = "Claude Code as a C++20 coroutine library";
    homepage = "https://github.com/lukeyeh/claude_code_co";
    platforms = lib.platforms.linux;
  };
}
