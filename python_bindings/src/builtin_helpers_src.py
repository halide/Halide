from abc import ABC, abstractmethod
from enum import Enum
from functools import total_ordering
from inspect import isclass
from typing import Any, Optional
import sys

# 'print()' is consumed by `halide.print()` which is implicitly present;
# here's our quick-n-dirty wrapper for debugging:
# def _print(*args):
#     def write(data):
#         sys.stdout.write(str(data))
#     for i, arg in enumerate(args):
#         if i:
#             write(" ")
#         write(arg)
#     write('\n')

# Everything below here is implicitly in the `halide` package

def _fail(msg: str):
    raise HalideError(msg)


def _check(cond: bool, msg: str):
    if not cond:
        _fail(msg)


def _is_valid_name(name: str) -> bool:
    # Basically, a valid C identifier, except:
    #
    # -- initial _ is forbidden (rather than merely "reserved")
    # -- two underscores in a row is also forbidden
    if not name:
        return False
    # TODO: use regex instead?
    s = str(name)
    if "__" in s:
        return False
    if not s[0].isalpha():
        return False
    for c in s[1:]:
        if not (c.isalnum() or c == "_"):
            return False
    return True


def _check_valid_name(name: str) -> str:
    _check(
        _is_valid_name(name),
        "The name '%s' is not valid for a member of GeneratorParams, Inputs, or Outputs."
        % name,
    )
    return name


def _get_data_member_names(o) -> list[str]:
    # Don't use dir(): it will alphabetize the result. It's critical that
    # we produce this list in order of declaration, which __dict__ should guarantee
    # in Python 3.7+.
    return [
        f for f in o.__class__.__dict__
        if not callable(getattr(o, f)) and not f.startswith("__")
    ]


def _normalize_type_list(types: object) -> list[Type]:
    # Always treat _NoneType as a non-type
    if types is None:
        types = []
    elif isinstance(types, Type):
        if types == _NoneType():
            types = []
        else:
            types = [types]
    for t in types:
        _check(isinstance(t, Type),
               "List-of-Type contains non-type item %s" % t)
    return types


_type_string_map = {
    # "bfloat16": BFloat(16),
    "bool": Bool(),
    "float16": Float(16),
    "float32": Float(32),
    "float64": Float(64),
    "int16": Int(16),
    "int32": Int(32),
    "int8": Int(8),
    "uint16": UInt(16),
    "uint32": UInt(32),
    "uint8": UInt(8),
}


def _parse_halide_type(s: str) -> Type:
    _check(s in _type_string_map,
           "The value %s cannot be parsed as a Type." % s)
    return _type_string_map[s]


def _parse_halide_type_list(s: str) -> list[Type]:
    return [_parse_halide_type(t) for t in s.split(",")]


def _parse_generatorparam_value(name: str, gp_type: type,
                                value: Any) -> object:
    if gp_type is str:
        return str(value)
    elif gp_type is bool:
        return bool(value)
    elif gp_type is int:
        return int(value)
    elif gp_type is float:
        return float(value)
    elif gp_type is Type:
        if isinstance(value, (list, Type)):
            value = _normalize_type_list(value)
        else:
            value = _parse_halide_type_list(str(value))
        return value
    else:
        _fail("GeneratorParam %s has unsupported type %s" % (name, gp_type))
        return None


class Requirement:
    # Name
    _name: str = ""

    # List of the required types, if any. An empty list means
    # no constraints. The list will usually be a single type,
    # except for Outputs that have Tuple-valued results, which
    # can have multiple types.
    _types: list[Type] = []

    # Required dimensions. 0 = scalar. -1 = unconstrained.
    _dims: int = -1

    def __init__(self, name: str, types: object, dimensions: int):
        self._name = _check_valid_name(name)
        self._types = _normalize_type_list(types)
        self._dims = dimensions
        _check(
            len(self._types) > 0,
            "No type has been specified for %s. Try specifying one in code, or by setting '%s.type' as a GeneratorParam."
            % (self._name, self._name),
        )
        _check(
            self._dims >= 0,
            "No dimensionality has been specified for %s. Try specifying one in code, or by setting '%s.dim' as a GeneratorParam."
            % (self._name, self._name),
        )

    def _check_types_and_dims(self, types: list[Type], dims: int):
        types = _normalize_type_list(types)
        assert len(types) > 0
        assert dims >= 0

        _check(
            len(types) == len(self._types),
            "Type mismatch for %s: expected %d types but saw %d" %
            (self._name, len(self._types), len(types)),
        )
        for i in range(0, len(types)):
            _check(
                self._types[i] == types[i],
                "Type mismatch for %s:%d: expected %s saw %s" %
                (self._name, i, self._types[i], types[i]),
            )
        _check(
            dims == self._dims,
            "Dimensions mismatch for %s: expected %d saw %d" %
            (self._name, self._dims, dims),
        )


class InputBuffer(ImageParam):

    def __init__(self, type: Optional[Type], dimensions: Optional[int]):
        if type is None:
            type = _NoneType()
        if dimensions is None:
            dimensions = -1
        ImageParam.__init__(self, type, dimensions, _unique_name())

    def _get_types_and_dims(self) -> (list[Type], int):
        return _normalize_type_list(self.type()), self.dimensions()

    def _get_direction_and_kind(self) -> (ArgInfoDirection, ArgInfoKind):
        return ArgInfoDirection.Input, ArgInfoKind.Buffer

    def _make_replacement(self, value: Any, r: Requirement) -> ImageParam:
        assert _is_valid_name(r._name) and len(r._types) == 1 and r._dims >= 0
        if value is None:
            return ImageParam(r._types[0], r._dims, r._name)

        _check(
            isinstance(value, (Buffer, ImageParam)),
            "Input %s requires an ImageParam or Buffer argument when using call(), but saw %s"
            % (r._name, str(value)),
        )
        _check(
            value.defined(),
            "Cannot set the value for %s to an undefined value." % r._name,
        )
        if isinstance(value, Buffer):
            im = ImageParam(value.type(), value.dimensions(), r._name)
            im.set(value)
            value = im
        r._check_types_and_dims([value.type()], value.dimensions())
        return value


# InputScalar looks like a Param
class InputScalar(Param):

    def __init__(self, type: Optional[Type]):
        if type is None:
            type = _NoneType()
        Param.__init__(self, type, _unique_name())

    def _get_types_and_dims(self) -> (list[Type], int):
        return _normalize_type_list(self.type()), 0

    def _get_direction_and_kind(self) -> (ArgInfoDirection, ArgInfoKind):
        return ArgInfoDirection.Input, ArgInfoKind.Scalar

    def _make_replacement(self, value: Any, r: Requirement) -> Param:
        assert _is_valid_name(r._name) and len(r._types) == 1 and r._dims == 0
        if value is None:
            return Param(r._types[0], r._name)

        if isinstance(value, (int, float, bool)):
            p = Param(r._types[0], r._name)
            p.set(value)
            return p
        else:
            _check(
                isinstance(value, Param),
                "Input %s requires a Param (or scalar literal) argument when using call(), but saw %s."
                % (r._name, str(value)),
            )
            _check(
                value.defined(),
                "Cannot set the value for %s to an undefined value." % r._name,
            )
            r._check_types_and_dims([value.type()], 0)
            return value


class OutputBuffer(Func):

    def __init__(self, types: Optional[Type], dimensions: Optional[int]):
        types = _normalize_type_list(types)
        if dimensions is None:
            dimensions = -1
        Func.__init__(self, types, dimensions, _unique_name())
        self._types = types
        self._dims = dimensions

    def _get_types_and_dims(self) -> (list[Type], int):
        return self._types, self._dims

    def _get_direction_and_kind(self) -> (ArgInfoDirection, ArgInfoKind):
        return ArgInfoDirection.Output, ArgInfoKind.Buffer

    def _make_replacement(self, value: Any, r: Requirement) -> Func:
        assert _is_valid_name(r._name) and len(r._types) > 0 and r._dims >= 0
        if value is None:
            return Func(r._types, r._dims, r._name)

        _check(
            isinstance(value, (Buffer, Func)),
            "Output %s requires a Func or Buffer argument when using call(), but saw %s"
            % (r._name, str(value)),
        )
        _check(
            value.defined(),
            "Cannot set the value for %s to an undefined value." % r._name,
        )

        if isinstance(value, Buffer):
            r._check_types_and_dims([value.type()], value.dimensions())
        else:
            r._check_types_and_dims(value.output_types(), value.dimensions())

        if isinstance(value, Buffer):
            # Allow assignment from a Buffer<> to an Output<Buffer<>>;
            # this allows us to use a statically-compiled buffer inside a Generator
            # to assign to an output.
            f = Func(r._types, r._dims, r._name)
            f[_] = value[_]
            value = f

        return value


# OutputScalar is just like OutputBuffer, except it is always dimensions = 0
class OutputScalar(OutputBuffer):

    def __init__(self, types: Optional[Type]):
        OutputBuffer.__init__(self, types, 0)

    def _make_replacement(self, value: Any, r: Requirement) -> Func:
        assert _is_valid_name(r._name) and len(r._types) > 0 and r._dims == 0
        if value is None:
            return Func(r._types, 0, r._name)

        _check(
            isinstance(value, Expr),
            "Output %s requires an Expr argument when using call(), but saw %s"
            % (r._name, str(value)),
        )
        _check(
            value.defined(),
            "Cannot set the value for %s to an undefined value." % r._name,
        )
        r._check_types_and_dims(value.type(), 0)
        f = Func(value.type(), 0, r._name)
        f[_] = value
        return f


@total_ordering
class _Stage(Enum):
    Created = 0
    GeneratorParamsBuilt = 1
    IOBuilt = 2
    IOFinalized = 3
    PipelineBuilt = 4

    def __lt__(self, other):
        if self.__class__ is other.__class__:
            return self.value < other.value
        return NotImplemented


# Prevent user code from inadvertently overwriting fields -- this is
# not at all foolproof but will make it inconvenient to do so.
# TODO: add ability to freeze ImageParam and Param so you can't call set(), etc.
def _make_freezable(cls: type):

    # TODO: kinda janky, is this the best way to implement this?
    def _freeze(self):
        setattr(self, "_frozen", True)

    def _freezable_setattr(self, name, value):
        if getattr(self, "_frozen", False):
            raise AttributeError("Invalid write to field '%s'" % name)
        self.__class__._old_halide_setattr(self, name, value)

    if not hasattr(cls, "_old_halide_setattr"):
        cls._old_halide_setattr = cls.__setattr__
        cls.__setattr__ = _freezable_setattr
        cls._freeze = _freeze


class Generator(ABC):
    """Base class for Halide Generators in Python"""

    def context(self):
        return self._context

    def target(self):
        return self.context().target()

    def auto_schedule(self):
        return self.context().auto_schedule()

    def machine_params(self):
        return self.context().machine_params()

    def natural_vector_size(self, type: Type) -> int:
        return self.target().natural_vector_size(type)

    # Inputs can be specified by either positional or named args,
    # but may not be mixed. (i.e., if any inputs are specified as a named
    # argument, they all must be specified that way; otherwise they must all be
    # positional, in the order declared in the Generator.)
    #
    # GeneratorParams can only be specified by name, and are always optional.
    @classmethod
    def call(cls, context: GeneratorContext, *args, **kwargs):
        _check(isinstance(context, GeneratorContext), "The first argument to call() must be a GeneratorContext")
        generator = cls(context)

        # Process the kwargs first: first, fill in all the GeneratorParams
        # (in case some are tied to Inputs). Just send all kwargs to
        # _set_generatorparam_value(); the ones that aren't valid will
        # be saved in _unhandled_generator_params and dealt with during Input processing.
        gp = kwargs.pop("generator_params", {})
        _check(isinstance(gp, dict), "generator_params must be a dict")
        for k, v in gp.items():
            generator._set_generatorparam_value(k, v)

        arginfos = generator._get_arginfos()
        input_arginfos = [
            a for a in arginfos if a.dir == ArgInfoDirection.Input
        ]
        output_arginfos = [
            a for a in arginfos if a.dir == ArgInfoDirection.Output
        ]

        # Now inputs:
        input_names = [a.name for a in input_arginfos]
        inputs_seen = []
        kw_inputs_specified = 0
        for k, v in kwargs.items():
            _check(k in input_names,
                   "Unknown input '%s' specified via keyword argument." % k)
            _check(not k in inputs_seen,
                   "Input %s specified multiple times." % k)
            inputs_seen.append(k)
            generator._bind_input(k, [v])
            kw_inputs_specified = kw_inputs_specified + 1

        if len(args) == 0:
            # No args specified positionally, so they must all be via keywords.
            _check(
                kw_inputs_specified == len(input_arginfos),
                "Expected exactly %d keyword args for inputs, but saw %d." %
                (len(input_arginfos), kw_inputs_specified),
            )
        else:
            # Some positional args, so all inputs must be positional (and none via keyword).
            _check(
                kw_inputs_specified == 0,
                "Cannot use both positional and keyword arguments for inputs.",
            )
            _check(
                len(args) == len(input_arginfos),
                "Expected exactly %d positional args for inputs, but saw %d." %
                (len(input_arginfos), len(args)),
            )
            for i in range(0, len(args)):
                a = input_arginfos[i]
                k = a.name
                v = args[i]
                _check(not k in inputs_seen,
                       "Input %s specified multiple times." % k)
                inputs_seen.append(k)
                generator._bind_input(k, [v])

        generator._build_pipeline()

        outputs = []
        for o in output_arginfos:
            outputs.extend(generator._get_output_func(o.name))

        return outputs[0] if len(outputs) == 1 else tuple(outputs)

    def __init__(self, context: GeneratorContext):
        _check(isinstance(context, GeneratorContext), "The first argument to Generator must be a GeneratorContext")

        self._context = context
        self._stage = _Stage.Created
        self._gp = None
        self._inputs = None
        self._outputs = None
        self._gp_names = None
        self._inputs_names = None
        self._outputs_names = None
        self._pipeline = None
        self._input_parameters = None
        self._output_funcs = None
        self._unhandled_generator_params = {}
        self._requirements = {}
        self._replacements = {}
        self._arginfos = None

        self._build_gp()

    @abstractmethod
    def generate(self, gp, i, o):
        pass

    def schedule(self, gp, i, o):
        pass

    def _finalize_types_and_dims(self, name: str, current_types: list[Type],
                                 current_dims: int) -> (list[Type], int):
        # Note that we create these for all relevant Inputs and Outputs, even though
        # some will be guaranteed to fail if you attempt to set them: that's the point,
        # we *want* things to fail if you try to set them inappropriately.
        u = self._unhandled_generator_params

        new_types = current_types
        new_dims = current_dims

        type_name = "%s.type" % name
        if type_name in u:
            value = u.pop(type_name)
            new_types = _parse_generatorparam_value(type_name, Type, value)
            _check(
                len(current_types) == 0,
                "Cannot set the GeneratorParam %s for %s because the value is explicitly specified in the Python source."
                % (type_name, self._get_name()),
            )

        dims_name = "%s.dim" % name
        if dims_name in u:
            value = u.pop(dims_name)
            new_dims = _parse_generatorparam_value(dims_name, int, value)
            _check(
                current_dims == -1,
                "Cannot set the GeneratorParam %s for %s because the value is explicitly specified in the Python source."
                % (dims_name, self._get_name()),
            )

        return new_types, new_dims

    class Empty:
        pass

    def _build_gp(self):
        assert self._stage == _Stage.Created
        assert not self._gp
        assert not self._inputs

        gp_class = getattr(self, "GeneratorParams", self.Empty)
        if not type(gp_class) is type:
            gp_class = gp_class()
        _make_freezable(gp_class)
        self._gp = gp_class()
        self._gp_names = _get_data_member_names(self._gp)
        for name in self._gp_names:
            _check_valid_name(name)
            value = getattr(self._gp, name)
            _check(
                isinstance(value, (str, bool, int, float, list, Type)),
                "GeneratorParam %s has unsupported type %s" %
                (name, type(value)),
            )
            # GeneratorParams don't use requirements at all, but this is a nearly-free way to check for unique names across GP + I + O
            _check(
                not name in self._requirements,
                "The name '%s' is used more than once in Generator %s." %
                (name, self._get_name()),
            )
            self._requirements[name] = None

        self._stage = _Stage.GeneratorParamsBuilt
        if hasattr(self, "_halide_alias_generator_params"):
            for k, v in self._halide_alias_generator_params.items():
                self._set_generatorparam_value(k, v)

    def _build_io(self):
        assert self._gp
        assert not self._inputs
        assert not self._outputs
        assert not self._arginfos
        assert self._stage == _Stage.GeneratorParamsBuilt

        # Freeze the GeneratorParams now
        self._gp._freeze()

        def _build_one_io(cls_name: str, legal_types: tuple[type]):
            io_class = getattr(self, cls_name, self.Empty)
            if not type(io_class) is type:
                io_class = io_class(self._gp)
            _make_freezable(io_class)
            io = io_class()
            io_names = _get_data_member_names(io)
            arginfos = []
            for name in io_names:
                # Names of Inputs and Outputs may have been stashed in _unhandled_generator_params
                # if our call() method was used. Quietly dispose of them here.
                self._unhandled_generator_params.pop(name, None)
                _check_valid_name(name)
                initial_io = getattr(io, name)
                _check(
                    isinstance(initial_io, legal_types),
                    "%s field %s has unsupported type %s" %
                    (cls_name, name, type(initial_io)),
                )
                _check(
                    not name in self._requirements,
                    "The name '%s' is used more than once in Generator %s." %
                    (name, self._get_name()),
                )
                types, dims = initial_io._get_types_and_dims()
                types, dims = self._finalize_types_and_dims(name, types, dims)
                r = Requirement(name, types, dims)
                # This just verifies that all types and dims are well-defined
                r._check_types_and_dims(types, dims)
                self._requirements[name] = r
                arginfos.append(
                    ArgInfo(
                        name,
                        *initial_io._get_direction_and_kind(),
                        types,
                        dims,
                    ))

            return io, io_names, arginfos

        self._inputs, self._inputs_names, inputs_arginfos = _build_one_io(
            "Inputs", (InputBuffer, InputScalar))
        self._outputs, self._outputs_names, outputs_arginfos = _build_one_io(
            "Outputs", (OutputBuffer, OutputScalar))
        self._arginfos = inputs_arginfos + outputs_arginfos
        _check(
            len(self._outputs_names) > 0,
            "Generator '%s' must declare at least one output." %
            self._get_name(),
        )
        _check(
            len(self._unhandled_generator_params) == 0,
            "Generator %s has no GeneratorParam(s) named: %s" %
            (self._get_name(), list(self._unhandled_generator_params.keys())),
        )

        self._stage = _Stage.IOBuilt

    def _finalize_io(self):
        if self._stage < _Stage.IOBuilt:
            self._build_io()
        assert self._stage == _Stage.IOBuilt

        def _finalize_one_io(io, io_names: list[str]):
            for name in io_names:
                # Note that new_io will be a different type (e.g. InputBuffer -> ImageParam)
                initial_io = getattr(io, name)
                new_io = self._replacements.pop(name, None)
                if not new_io:
                    new_io = initial_io._make_replacement(
                        None, self._requirements[name])
                setattr(io, name, new_io)
            # Freeze the IO now
            io._freeze()

        _finalize_one_io(self._inputs, self._inputs_names)
        _finalize_one_io(self._outputs, self._outputs_names)

        self._stage = _Stage.IOFinalized

    # --------------- AbstractGenerator methods (partial)
    def _get_name(self) -> str:
        return self._halide_registered_name

    def _get_arginfos(self) -> list[ArgInfo]:
        if self._stage < _Stage.IOBuilt:
            self._build_io()
        assert self._stage >= _Stage.IOBuilt
        return self._arginfos

    def _set_generatorparam_value(self, name: str, value: Any):
        assert self._stage == _Stage.GeneratorParamsBuilt
        assert not self._pipeline
        assert not self._inputs
        assert not self._outputs
        if name in self._gp_names:
            old_value = getattr(self._gp, name)
            new_value = _parse_generatorparam_value(name, type(old_value),
                                                    value)
            setattr(self._gp, name, new_value)
        else:
            self._unhandled_generator_params[name] = value

    def _build_pipeline(self) -> Pipeline:
        if self._stage <= _Stage.IOFinalized:
            self._finalize_io()

        assert self._stage == _Stage.IOFinalized
        assert not self._pipeline
        assert not self._input_parameters
        assert not self._output_funcs

        self.generate(self._gp, self._inputs, self._outputs)
        self.schedule(self._gp, self._inputs, self._outputs)

        self._input_parameters = {
            n: getattr(self._inputs, n).parameter()
            for n in self._inputs_names
        }
        self._output_funcs = {
            n: getattr(self._outputs, n)
            for n in self._outputs_names
        }
        _check(
            len(self._output_funcs) > 0,
            "Generator %s must declare at least one Output in Arguments." %
            self._get_name(),
        )

        funcs = []
        for name, f in self._output_funcs.items():
            _check(f.defined(), "Output '%s' was not defined." % name)
            self._requirements[name]._check_types_and_dims(
                f.output_types(), f.dimensions())
            funcs.append(f)

        self._pipeline = Pipeline(funcs)
        self._stage = _Stage.PipelineBuilt
        return self._pipeline

    def _get_input_parameter(self, name: str) -> list[InternalParameter]:
        assert self._stage == _Stage.PipelineBuilt
        _check(name in self._input_parameters, "Unknown input: %s" % name)
        return [self._input_parameters[name]]

    def _get_output_func(self, name: str) -> list[Func]:
        assert self._stage == _Stage.PipelineBuilt
        _check(name in self._output_funcs, "Unknown output: %s" % name)
        return [self._output_funcs[name]]

    def _bind_input(self, name: str, values: list[object]):
        assert (self._stage == _Stage.GeneratorParamsBuilt
                or self._stage == _Stage.IOBuilt)
        _check(
            len(values) == 1, "Too many values specified for input: %s" % name)
        _check(name in self._inputs_names,
               "There is no input with the name: %s" % name)
        g = getattr(self._inputs, name)
        assert not name in self._replacements
        self._replacements[name] = g._make_replacement(
            values[0], self._requirements[name])


_python_generators: dict = {}


def _get_python_generator_names() -> list[str]:
    return _python_generators.keys()


def _fqname(o):
    k = o
    m = k.__module__
    q = k.__qualname__
    if m == "__main__" or k == "builtins":
        return q
    return m + "." + q


def _find_python_generator_class(name: str):
    cls = _python_generators.get(name, None)
    if not isclass(cls):
        cls = None
    return cls


def alias(**kwargs):

    def alias_impl(cls):
        for k, v in kwargs.items():
            _check_valid_name(k)
            _check(hasattr(cls, "_halide_registered_name"),
                   "@alias can only be used in conjunction with @generator.")
            _check(not k in _python_generators,
                   "The Generator name %s is already in use." % k)
            _check(
                type(v) is dict,
                "The Generator alias %s specifies something other than a dict."
                % k)
            new_cls = type(k, (cls, ), {
                "_halide_registered_name": k,
                "_halide_alias_generator_params": v
            })
            _python_generators[k] = new_cls
        return cls

    return alias_impl


def generator(name=""):
    # This code relies on dicts preserving key-insertion order, which is only
    # guaranteed for all Python implementations as of v3.7.
    assert sys.version_info >= (
        3, 7), "Halide Generators require Python 3.7 or later."

    def generator_impl(cls):
        n = name if name else _fqname(cls)
        _check(not n in _python_generators,
               "The Generator name %s is already in use." % n)
        _check(isclass(cls), "@generator can only be used on classes.")
        _check(
            not issubclass(cls, Generator),
            "Please use the @generator decorator instead of inheriting from hl.Generator",
        )
        new_cls = type(cls.__name__, (cls, Generator),
                       {"_halide_registered_name": n})
        _python_generators[n] = new_cls
        return new_cls

    return generator_impl
