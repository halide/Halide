def exec_test(name, cmd, data):
    contents = 'set -eu && echo \\"\$$1\\" | bash'

    if type(cmd) == 'list':
        cmd = " ; ".join(cmd)
    cmd = cmd.replace('"', '\\"')

    native.genrule(
        name = "%s_gen_sh" % name,
        outs = ["%s_gen.sh" % name],
        cmd = 'echo "%s" > $@ && chmod +x $@' % contents,
        srcs = data
    )

    native.sh_test(
        name = name,
        srcs = ["%s_gen.sh" % name],
        args = ['"' + cmd + '"'],
        data = data,
    )
