# Description:
#  Private Skylark helper functions for setting up the @llvm repository used by Halide.
#  Should not be used by code outside of Halide itself.

def _llvm_config(repository_ctx, cfg, arg):
  result = repository_ctx.execute([cfg, arg])
  if result.return_code != 0:
    fail("%s %s failed: %d" % (cfg, arg, result.return_code))
  return result.stdout.strip()

def _find_locally_or_download_impl(repository_ctx):
  if 'LLVM_CONFIG' in repository_ctx.os.environ:
    cfg = repository_ctx.os.environ['LLVM_CONFIG']
    if cfg == "":
      fail("LLVM_CONFIG set, but empty")
  else:
    fail("You must have LLVM_CONFIG set in order to build Halide with Bazel.")
    # TODO: something along these lines is probably workable, but needs some thought
    # repository_ctx.download_and_extract(
    #   "http://llvm.org/releases/3.9.0/llvm-3.9.0.src.tar.xz",
    #   ".", 
    #   "66c73179da42cee1386371641241f79ded250e117a79f571bbd69e56daa48948", 
    #   "", 
    #   "llvm-3.9.0.src"
    # )
    # repository_ctx.download_and_extract(
    #   "http://llvm.org/releases/3.9.0/cfe-3.9.0.src.tar.xz",
    #   "tools/clang", 
    #   "7596a7c7d9376d0c89e60028fe1ceb4d3e535e8ea8b89e0eb094e0dcb3183d28", 
    #   "", 
    #   "cfe-3.9.0.src"
    # )
    # repository_ctx.execute(['cmake', 
    #             '-Bllvm_build',
    #             '-H.',
    #             '-DLLVM_ENABLE_TERMINFO=OFF',
    #             '-DLLVM_TARGETS_TO_BUILD="X86;ARM;NVPTX;AArch64;Mips;PowerPC;Hexagon"',
    #             '-DLLVM_ENABLE_ASSERTIONS=ON',
    #             '-DCMAKE_BUILD_TYPE=Release'])
    # cfg = "#TODO"

  repository_ctx.symlink(Label("//tools:llvm.BUILD"), "BUILD")

  repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--bindir'), 'bin')
  repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--libdir'), 'lib')
  repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--includedir'), 'include')
  repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--libdir') + '/../include', 'build_include')
  repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--libdir') + '/../lib', 'static_libs')
  llvm_version = _llvm_config(repository_ctx, cfg, '--version')
  llvm_components = _llvm_config(repository_ctx, cfg, '--components').split(' ')
  llvm_static_libs = _llvm_config(repository_ctx, cfg, '--libnames').split(' ')
  llvm_ldflags = _llvm_config(repository_ctx, cfg, '--ldflags').split(' ')
  llvm_cxxflags = _llvm_config(repository_ctx, cfg, '--cxxflags').split(' ')
  llvm_system_libs = _llvm_config(repository_ctx, cfg, '--system-libs').split(' ')

  repository_ctx.template(
    "llvm_internal_build_defs.bzl",
    Label("//tools:llvm_internal_build_defs.bzl.tpl"),
    {
      "%{llvm_version}": repr(llvm_version[0] + llvm_version[2]),
      "%{llvm_components}": repr(llvm_components),
      "%{llvm_static_libs}": repr(llvm_static_libs),
      "%{llvm_system_libs}": repr(llvm_system_libs),
      "%{llvm_ldflags}": repr(llvm_ldflags),
      "%{llvm_cxxflags}": repr(llvm_cxxflags),
    },
    False
  ) 

_find_locally_or_download = repository_rule(
  implementation = _find_locally_or_download_impl,
  local = True,
)

def llvm_repository():
  _find_locally_or_download(name = "llvm")
