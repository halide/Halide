# Description:
#  Private Skylark helper functions for setting up the @llvm repository used by Halide.
#  Should not be used by code outside of Halide itself.

# TODO: llvm-config will return windows-style paths on windows;
# we must convert to msys2-style paths. This is ugly. 
# def _from_windows_path(s):
#   if len(s) >= 3:
#     if s[0].isalpha() and s[1] == ':' and s[2] == '\\':
#       return '/%s/%s' % (s[0].lower(), s[3:].replace('\\', '/'))
#   return s

# Given a path that might be a Windows-style full path (or a leaf) to a lib:
#    c:\foo\bar.lib
#    baz.lib
# convert to a linkopt:
#    -Wl,bar.lib
#    -Wl,baz.lib
def _from_windows_lib_path(s):
  if not s.endswith(".lib"):
    return s
  return "-Wl," + s.split("\\")[-1]

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

  llvm_version = _llvm_config(repository_ctx, cfg, '--version')
  llvm_components = _llvm_config(repository_ctx, cfg, '--components').split(' ')
  llvm_libs = _llvm_config(repository_ctx, cfg, '--libs').split(' ')
  llvm_static_libs = _llvm_config(repository_ctx, cfg, '--libnames').split(' ')
  llvm_ldflags = _llvm_config(repository_ctx, cfg, '--ldflags').split(' ')
  llvm_cxxflags = _llvm_config(repository_ctx, cfg, '--cxxflags').split(' ')
  llvm_system_libs = _llvm_config(repository_ctx, cfg, '--system-libs').split(' ')
  llvm_obj_root = _llvm_config(repository_ctx, cfg, '--obj-root')
  llvm_src_root = _llvm_config(repository_ctx, cfg, '--src-root')

  repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--bindir'), 'bin')
  repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--libdir'), 'lib')

  # llvm-config --includedir returns the wrong path on Windows.
  # repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--includedir'), 'include')
  repository_ctx.symlink(llvm_src_root + '/include', 'include')

  # TODO: no llvm-config flag to get this directly (since it's provided indirectly
  # via the -cxxflags). That isn't helpful for us, since Bazel strips out the
  # rogue -I directives, so make an assumption about layout; to make matters worse,
  # there are directory layout differences on Windows that I haven't found an elegant
  # way to abstract.
  rel_path = "/../../include" if cfg.endswith(".exe") else "/../include"
  repository_ctx.symlink(_llvm_config(repository_ctx, cfg, '--libdir') + rel_path, 'build_include')

  # Mangle Windows paths as necessary
  llvm_libs = [_from_windows_lib_path(p) for p in llvm_libs]
  llvm_system_libs = [_from_windows_lib_path(p) for p in llvm_system_libs]

  repository_ctx.template(
    "llvm_internal_build_defs.bzl",
    Label("//tools:llvm_internal_build_defs.bzl.tpl"),
    {
      "%{llvm_executable_extension}": repr(".exe" if cfg.endswith(".exe") else ""),
      "%{llvm_version}": repr(llvm_version[0] + llvm_version[2]),
      "%{llvm_components}": repr(llvm_components),
      "%{llvm_libs}": repr(llvm_libs),
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
