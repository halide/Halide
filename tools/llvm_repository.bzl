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

# repository_ctx.execute() requires a Windows-style path on Windows;
# if the path appears to be an msys2-style path, convert.
def _to_windows_path(s):
  if len(s) >= 3:
    if s[0] == '/' and s[1].isalpha() and s[2] == '/':
      return '%s:/%s' % (s[1].lower(), s[3:])
  return s

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
    fail("%s %s failed: %d (%s)" % (cfg, arg, result.return_code, result.stderr))
  # print("%s %s returns (%s)" % (cfg, arg, result.stdout.strip()))
  r = result.stdout.strip()

  if cfg.endswith(".exe"):
    # Transmute all " /" -> " -" so that flags specified as /FLAG become -FLAG, to make MSYS2 happy as well as cmd.exe;
    # this is arbitrary, but effective (in practice, any value from llvm-config with a / will be a flag)
    r = r.replace(' /', ' -')

    # Transmute all \ -> / so that the resulting paths will work in MSYS2 as well as cmd.exe;
    # this is arbitrary, but effective (in practice, any value from llvm-config with a \ will be a path)
    r = r.replace('\\', '/')

  return r

def _configure_llvm_impl(repository_ctx):
  if 'LLVM_CONFIG' not in repository_ctx.os.environ:
    fail("You must have LLVM_CONFIG set in order to build Halide with Bazel.")

  cfg = repository_ctx.os.environ['LLVM_CONFIG']
  if cfg == "":
    fail("You must have LLVM_CONFIG set in order to build Halide with Bazel.")

  cfg = _to_windows_path(cfg)

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

_configure_llvm = repository_rule(
  implementation = _configure_llvm_impl,
  local = True,
)

def llvm_repository():
  _configure_llvm(name = "llvm")
