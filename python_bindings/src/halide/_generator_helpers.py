from __future__ import annotations
from abc import ABC, abstractmethod
from contextvars import ContextVar
from enum import Enum
from functools import total_ordering
from .halide_ import *
from .halide_ import _unique_name, _UnspecifiedType
from inspect import isclass
from typing import Any, Optional
import builtins
import re
import sys
import warnings

def _fail(msg: str):
    raise HalideError(msg)


def _check(cond: bool, msg: str):
    if not cond:
        _fail(msg)

# Basically, a valid C identifier, except:
#
# -- initial _ is forbidden (rather than merely "reserved")
# -- two underscores in a row is also forbidden
_NAME_RE = re.compile(r"^(?!.*__)[A-Za-z0-9][A-Za-z0-9_]*$")

def _is_valid_name(name: str) -> bool:
    if not name:
        return False
    # We forbid this to avoid ambiguity in arguments to call()
    if name == "generator_params":
        return False

    return _NAME_RE.search(name)


def _check_valid_name(name: str) -> str:
    _check(
        _is_valid_name(name),
        "The name '%s' is not valid for a GeneratorParam, Input, or Output." % name,
    )
    return name


# Transmute 'None' into our internal "UnspecifiedType" as a placeholder;
# also add some syntactic sugar, to allow users to alias
# bool -> UInt(1), int -> Int(32), float -> Float(32)
def _sanitize_type(t: object) -> Type:
    if t is None or t is type(None):
        return _UnspecifiedType()
    elif t is bool:
        return UInt(1)
    elif t is int:
        return Int(32)
    elif t is float:
        return Float(32)
    else:
        _check(isinstance(t, Type), "Expected a Halide Type, but saw: %s" % t)
        return t

def _normalize_type_list(types: object) -> list[Type]:
    # Always treat _UnspecifiedType as a non-type
    if types is None:
        types = []
    elif isinstance(types, Type) and types == _UnspecifiedType():
        types = []
    if type(types) is not list:
        types = [types];
    types = [_sanitize_type(t) for t in types]
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
    _check(s in _type_string_map, "The value %s cannot be parsed as a Type." % s)
    return _type_string_map[s]


def _parse_halide_type_list(s: str) -> list[Type]:
    return [_parse_halide_type(t) for t in s.split(",")]


class Requirement:
    # Name
    _name: str = ""

    # List of the required types, if any. An empty list means
    # no constraints. The list will usually be a single type,
    # except for Outputs that have Tuple-valued results, which
    # can have multiple types.
    _types: list[Type] = []

    # Required dimensions. 0 = scalar. -1 = unconstrained.
    _dimensions: int = -1

    def __init__(self, name: str, types: object, dimensions: int):
        self._name = _check_valid_name(name)
        self._types = _normalize_type_list(types)
        self._dimensions = dimensions
        _check(
            len(self._types) > 0,
            "No type has been specified for %s. Try specifying one in code, or by setting '%s.type' as a GeneratorParam."
            % (self._name, self._name),
        )
        _check(
            self._dimensions >= 0,
            "No dimensionality has been specified for %s. Try specifying one in code, or by setting '%s.dim' as a GeneratorParam."
            % (self._name, self._name),
        )
        self._check_types_and_dimensions(types, dimensions)

    def _check_types_and_dimensions(self, types: list[Type], dimensions: int):
        types = _normalize_type_list(types)
        assert len(types) > 0
        assert dimensions >= 0

        _check(
            len(types) == len(self._types),
            "Type mismatch for %s: expected %d types but saw %d" % (self._name, len(self._types), len(types)),
        )
        for i in range(0, len(types)):
            _check(
                self._types[i] == types[i],
                "Type mismatch for %s:%d: expected %s saw %s" % (self._name, i, self._types[i], types[i]),
            )
        _check(
            dimensions == self._dimensions,
            "Dimensions mismatch for %s: expected %d saw %d" % (self._name, self._dimensions, dimensions),
        )


# GeneratorParam is a string, int, float, bool, Type
class GeneratorParam:
    # This tells PyType that we dynamically set attributes
    # on this object. (We don't, actually, but it defeats
    # PyType's complaints about using len(), arithmetic operators, etc.
    # on GeneratorParam fields, since they are replaced at runtime.)
    _HAS_DYNAMIC_ATTRIBUTES:bool = True

    _name: str = ""
    _value: Any = None

    def __init__(self, value: Any):
        # Don't parse it here, since we don't have our name yet
        self._value = value

    @staticmethod
    def _parse_value(name: str, gp_type: type, value: Any) -> object:
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

    def _make_replacement(self, value: Any, r: Requirement) -> Any:
        assert _is_valid_name(r._name)

        # Check the default value for validity
        GeneratorParam._parse_value(r._name, type(self._value), self._value)

        if value is not None:
            # parse replacement value for validity, and also for type match with default.
            return GeneratorParam._parse_value(r._name, type(self._value), value)
        else:
            return self._value

    def _get_types_and_dimensions(self) -> (list[Type], int):
        # Use dummy type-and-dimensions here so that the ctor won't fail due to undefined status
        return [Int(32)], 0


class InputBuffer(ImageParam):

    def __init__(self, type: Optional[Type], dimensions: Optional[int]):
        type = _sanitize_type(type)
        if dimensions is None:
            dimensions = -1
        super().__init__(type, dimensions, _unique_name())

    def _get_types_and_dimensions(self) -> (list[Type], int):
        return _normalize_type_list(self.type()), self.dimensions()

    def _get_direction_and_kind(self) -> (ArgInfoDirection, ArgInfoKind):
        return ArgInfoDirection.Input, ArgInfoKind.Buffer

    def _make_replacement(self, value: Any, r: Requirement) -> ImageParam:
        assert _is_valid_name(r._name) and len(r._types) == 1 and r._dimensions >= 0
        if value is None:
            return ImageParam(r._types[0], r._dimensions, r._name)

        _check(
            isinstance(value, (Buffer, ImageParam)),
            "Input %s requires an ImageParam or Buffer argument when using call(), but saw %s" % (r._name, str(value)),
        )
        _check(
            value.defined(),
            "Cannot set the value for %s to an undefined value." % r._name,
        )
        if isinstance(value, Buffer):
            im = ImageParam(value.type(), value.dimensions(), r._name)
            im.set(value)
            value = im
        r._check_types_and_dimensions([value.type()], value.dimensions())
        return value


class InputScalar(Param):

    def __init__(self, type: Optional[Type]):
        type = _sanitize_type(type)
        super().__init__(type, _unique_name())

    def _get_types_and_dimensions(self) -> (list[Type], int):
        return _normalize_type_list(self.type()), 0

    def _get_direction_and_kind(self) -> (ArgInfoDirection, ArgInfoKind):
        return ArgInfoDirection.Input, ArgInfoKind.Scalar

    def _make_replacement(self, value: Any, r: Requirement) -> Param:
        assert _is_valid_name(r._name) and len(r._types) == 1 and r._dimensions == 0
        if value is None:
            return Param(r._types[0], r._name)

        if isinstance(value, (int, float, bool)):
            p = Param(r._types[0], r._name)
            p.set(value)
            return p
        else:
            _check(
                isinstance(value, Param),
                "Input %s requires a Param (or scalar literal) argument when using call(), but saw %s." %
                (r._name, str(value)),
            )
            _check(
                value.defined(),
                "Cannot set the value for %s to an undefined value." % r._name,
            )
            r._check_types_and_dimensions([value.type()], 0)
            return value


class OutputBuffer(Func):

    def __init__(self, types: Optional[Type], dimensions: Optional[int]):
        types = _normalize_type_list(types)
        if dimensions is None:
            dimensions = -1
        super().__init__(types, dimensions, _unique_name())
        self._types = types
        self._dimensions = dimensions

    def _get_types_and_dimensions(self) -> (list[Type], int):
        return self._types, self._dimensions

    def _get_direction_and_kind(self) -> (ArgInfoDirection, ArgInfoKind):
        return ArgInfoDirection.Output, ArgInfoKind.Buffer

    def _make_replacement(self, value: Any, r: Requirement) -> Func:
        assert _is_valid_name(r._name) and len(r._types) > 0 and r._dimensions >= 0
        if value is None:
            return Func(r._types, r._dimensions, r._name)

        _check(
            isinstance(value, (Buffer, Func)),
            "Output %s requires a Func or Buffer argument when using call(), but saw %s" % (r._name, str(value)),
        )
        _check(
            value.defined(),
            "Cannot set the value for %s to an undefined value." % r._name,
        )

        value_types = [value.type()] if isinstance(value, Buffer) else value.output_types
        r._check_types_and_dimensions(value_types, value.dimensions())

        if isinstance(value, Buffer):
            # Allow assignment from a Buffer<> to an Output<Buffer<>>;
            # this allows us to use a statically-compiled buffer inside a Generator
            # to assign to an output.
            f = Func(r._types, r._dimensions, r._name)
            f[_] = value[_]
            value = f

        return value


# OutputScalar is just like OutputBuffer, except it is always dimensions = 0
class OutputScalar(OutputBuffer):

    def __init__(self, types: Optional[Type]):
        super().__init__(types, 0)

    def _make_replacement(self, value: Any, r: Requirement) -> Func:
        assert _is_valid_name(r._name) and len(r._types) > 0 and r._dimensions == 0
        if value is None:
            return Func(r._types, 0, r._name)

        _check(
            isinstance(value, Expr),
            "Output %s requires an Expr argument when using call(), but saw %s" % (r._name, str(value)),
        )
        _check(
            value.defined(),
            "Cannot set the value for %s to an undefined value." % r._name,
        )
        r._check_types_and_dimensions(value.type(), 0)
        f = Func(value.type(), 0, r._name)
        f[_] = value
        return f


@total_ordering
class _Stage(Enum):
    generator_created = 0
    gp_created = 1
    gp_replaced = 2
    io_created = 3
    configure_called = 4
    io_replaced = 5
    pipeline_built = 6

    def __lt__(self, other):
        if self.__class__ is other.__class__:
            return self.value < other.value
        return NotImplemented


def _unsorted_cls_dir(cls):
    for base in cls.__bases__:
        yield from _unsorted_cls_dir(base)

    # Don't use dir(): it will alphabetize the result. It's critical that
    # we produce this list in order of declaration, which __dict__ should guarantee
    # in Python 3.7+.
    for k, v in cls.__dict__.items():
        yield (k, v)


_halide_generator_context = ContextVar('halide_generator_context', default=None)


def _generatorcontext_enter(self: GeneratorContext) -> GeneratorContext:
    if not hasattr(self, "_tokens"):
        self._tokens = []
    self._tokens.append(_halide_generator_context.set(self))
    return self


def _generatorcontext_exit(self: GeneratorContext) -> None:
    _halide_generator_context.reset(self._tokens.pop())


class Generator(ABC):
    """Base class for Halide Generators in Python"""

    def context(self):
        return GeneratorContext(self._target, self._autoscheduler)

    def target(self):
        return self._target

    def autoscheduler(self):
        return self._autoscheduler

    def using_autoscheduler(self):
        return bool(self._autoscheduler.name)

    def natural_vector_size(self, type: Type) -> int:
        return self.target().natural_vector_size(type)

    def add_requirement(self, condition: Expr, *args) -> None:
        assert self._stage < _Stage.pipeline_built
        self._pipeline_requirements.append((condition, [*args]))

    @classmethod
    def call(cls, *args, **kwargs):
        generator = cls()

        # First, fill in all the GeneratorParams
        # (in case some are tied to Inputs).
        gp = kwargs.pop("generator_params", {})
        _check(isinstance(gp, dict), "generator_params must be a dict")
        for k, v in gp.items():
            generator._set_generatorparam_value(k, v)

        generator._advance_to_stage(_Stage.configure_called)

        _check(
            len(args) <= len(generator._arginfos_in),
            "Generator '%s' allows at most %d positional args, but %d were specified." %
            (generator._get_name(), len(generator._arginfos_in), len(args)))

        inputs_set = []
        for i in range(0, len(args)):
            a = generator._arginfos_in[i]
            k = a.name
            v = args[i]
            _check(k not in inputs_set, "Input %s was specified multiple times." % k)
            inputs_set.append(k)
            generator._bind_input(k, [v])

        input_names = [a.name for a in generator._arginfos_in]
        for k, v in kwargs.items():
            _check(k in input_names, "Unknown input '%s' specified via keyword argument." % k)
            _check(k not in inputs_set, "Input %s specified multiple times." % k)
            inputs_set.append(k)
            generator._bind_input(k, [v])

        _check(
            len(inputs_set) == len(generator._arginfos_in), "Generator '%s' requires %d args, but %d were specified." %
            (generator._get_name(), len(generator._arginfos_in), len(inputs_set)))

        generator._build_pipeline()

        outputs = []
        for o in generator._arginfos_out:
            outputs.append(generator._get_output_func(o.name))

        return outputs[0] if len(outputs) == 1 else tuple(outputs)

    def compile_to_callable(self):
        pipeline = self._build_pipeline()
        arguments = [
            self._get_input_parameter(a.name)._to_argument()
            for a in self._get_arginfos()
            if a.dir == ArgInfoDirection.Input
        ]
        return pipeline.compile_to_callable(arguments, self._target)

    # Make it hard for the user to overwrite any members that are GeneratorParams, Inputs, or Outputs
    def __setattr__(self, name, value):
        r = getattr(self, "_requirements", None)
        s = getattr(self, "_stage", None)
        if r and s and (name in r) and (s != _Stage.configure_called) and (s != _Stage.gp_created):
            raise AttributeError("Invalid write to field '%s'" % name)
        super().__setattr__(name, value)

    def __init__(self, generator_params: dict = {}):
        context = active_generator_context()

        self._target = context.target()
        self._autoscheduler = context.autoscheduler_params()
        self._stage = _Stage.generator_created
        self._gp_dict = None
        self._inputs_dict = None
        self._outputs_dict = None
        self._arginfos_in = None
        self._arginfos_out = None
        self._pipeline = None
        self._input_parameters = None
        self._output_funcs = None
        self._unhandled_generator_params = {}
        self._requirements = {}
        self._replacements = {}
        self._in_configure = 0
        self._pipeline_requirements = []

        self._advance_to_gp_created()
        if generator_params:
            _check(isinstance(generator_params, dict), "generator_params must be a dict")
            for k, v in generator_params.items():
                self._set_generatorparam_value(k, v)

    def allow_out_of_order_inputs_and_outputs(self):
        return False

    def configure(self):
        pass

    @abstractmethod
    def generate(self):
        pass

    def _add_gpio(self, name: str, io, io_dict: dict, arginfos: Optional[list]) -> None:
        _check_valid_name(name)
        _check(
            name not in self._requirements,
            "The name '%s' is used more than once in Generator %s." % (name, self._get_name()),
        )
        types, dimensions = io._get_types_and_dimensions()
        types, dimensions = self._set_io_types_and_dimensions_from_gp(name, types, dimensions)
        r = Requirement(name, types, dimensions)
        self._requirements[name] = r
        io_dict[name] = io
        if arginfos is not None:
            arginfos.append(ArgInfo(name, *io._get_direction_and_kind(), types, dimensions))

    def add_input(self, name: str, io) -> None:
        _check(self._in_configure > 0, "Can only call add_input() from the configure() method.")
        _check(not hasattr(self, name),
               "Cannot call add_input('%s') because the class already has a member of that name." % name)
        _check(isinstance(io, (InputBuffer, InputScalar)),
               "Cannot call add_input() with an object of type '%s'." % type(io))
        self._add_gpio(name, io, self._inputs_dict, self._arginfos_in)

    def add_output(self, name: str, io) -> None:
        _check(self._in_configure > 0, "Can only call add_output() from the configure() method.")
        _check(not hasattr(self, name),
               "Cannot call add_output('%s') because the class already has a member of that name." % name)
        _check(isinstance(io, (OutputBuffer, OutputScalar)),
               "Cannot call add_output() with an object of type '%s'." % type(io))
        self._add_gpio(name, io, self._outputs_dict, self._arginfos_out)

    def _advance_to_stage(self, new_stage: _Stage):
        _stage_advancers = {
            _Stage.gp_created: self._advance_to_gp_created,
            _Stage.gp_replaced: self._advance_to_gp_replaced,
            _Stage.io_created: self._advance_to_io_created,
            _Stage.configure_called: self._advance_to_configure_called,
            _Stage.io_replaced: self._advance_to_io_replaced,
        }
        assert new_stage in _stage_advancers
        a = _stage_advancers[new_stage]
        old_stage = self._stage
        if self._stage < new_stage:
            a()
        assert self._stage >= new_stage

    def _advance_to_gp_created(self):
        assert self._stage < _Stage.gp_created
        assert not self._gp_dict
        assert not self._inputs_dict
        assert not self._outputs_dict

        self._gp_dict = {}
        for name, gp in _unsorted_cls_dir(self.__class__):
            if not isinstance(gp, GeneratorParam):
                continue
            self._add_gpio(name, gp, self._gp_dict, None)

        self._stage = _Stage.gp_created

        if hasattr(self, "_halide_alias_generator_params"):
            for k, v in self._halide_alias_generator_params.items():
                self._set_generatorparam_value(k, v)

    def _advance_to_io_created(self):
        self._advance_to_stage(_Stage.gp_replaced)
        assert self._gp_dict is not None
        assert not self._inputs_dict
        assert not self._outputs_dict

        self._inputs_dict = {}
        self._outputs_dict = {}
        self._arginfos_in = []
        self._arginfos_out = []
        outputs_seen = False
        for name, io in _unsorted_cls_dir(self.__class__):
            is_input = isinstance(io, (InputBuffer, InputScalar))
            is_output = isinstance(io, (OutputBuffer, OutputScalar))
            if not (is_input or is_output):
                continue

            if is_input and outputs_seen and not self.allow_out_of_order_inputs_and_outputs():
                io_order_warning = ("Generators will always produce code that orders all Inputs before all Outputs; "
                                   "this Generator declares the Inputs and Outputs in a different order, so the calling convention may not be as expected. "
                                   "A future version of Halide will make this illegal, and require all Inputs to be declared before all Outputs. "
                                   "(You can avoid this requirement by overriding Generator::allow_out_of_order_inputs_and_outputs().)")
                warnings.warn(io_order_warning)

            if is_output:
                outputs_seen = True
            d = self._inputs_dict if is_input else self._outputs_dict
            a = self._arginfos_in if is_input else self._arginfos_out
            self._add_gpio(name, io, d, a)

        _check(
            len(self._unhandled_generator_params) == 0,
            "Generator %s has no GeneratorParam(s) named: %s" %
            (self._get_name(), list(self._unhandled_generator_params.keys())),
        )

        self._stage = _Stage.io_created

    def _set_io_types_and_dimensions_from_gp(self, name: str, current_types: list[Type],
                                             current_dimensions: int) -> (list[Type], int):
        new_types = current_types
        new_dimensions = current_dimensions

        type_name = "%s.type" % name
        if type_name in self._unhandled_generator_params:
            value = self._unhandled_generator_params.pop(type_name)
            new_types = GeneratorParam._parse_value(type_name, Type, value)
            _check(
                len(current_types) == 0,
                "Cannot set the GeneratorParam %s for %s because the value is explicitly specified in the Python source."
                % (type_name, self._get_name()),
            )

        dimensions_name = "%s.dim" % name
        if dimensions_name in self._unhandled_generator_params:
            value = self._unhandled_generator_params.pop(dimensions_name)
            new_dimensions = GeneratorParam._parse_value(dimensions_name, int, value)
            _check(
                current_dimensions == -1,
                "Cannot set the GeneratorParam %s for %s because the value is explicitly specified in the Python source."
                % (dimensions_name, self._get_name()),
            )

        return new_types, new_dimensions

    def _advance_to_configure_called(self):
        self._advance_to_stage(_Stage.io_created)

        self._in_configure += 1
        self.configure()
        self._in_configure -= 1

        _check(
            len(self._outputs_dict) > 0,
            "Generator '%s' must declare at least one output." % self._get_name(),
        )

        self._stage = _Stage.configure_called

    def _replace_one(self, gpio_dict):
        for name, gpio in gpio_dict.items():
            # Note that new_io will be a different type (e.g. InputBuffer -> ImageParam)
            new_gpio = self._replacements.pop(name, None)
            if not new_gpio:
                new_gpio = gpio._make_replacement(None, self._requirements[name])
            setattr(self, name, new_gpio)

    def _advance_to_gp_replaced(self):
        self._advance_to_stage(_Stage.gp_created)
        self._replace_one(self._gp_dict)
        self._stage = _Stage.gp_replaced

    def _advance_to_io_replaced(self):
        self._advance_to_stage(_Stage.configure_called)
        self._replace_one(self._inputs_dict)
        self._replace_one(self._outputs_dict)
        self._stage = _Stage.io_replaced

    # --------------- AbstractGenerator methods (partial)
    def _get_name(self) -> str:
        return self._halide_registered_name

    def _get_arginfos(self) -> list[ArgInfo]:
        self._advance_to_stage(_Stage.configure_called)
        return self._arginfos_in + self._arginfos_out

    def _set_generatorparam_value(self, name: str, value: Any):
        _check(
            name != "target",
            "The GeneratorParam named %s cannot be set by set_generatorparam_value." % name,
        )
        assert self._stage == _Stage.gp_created
        assert not self._pipeline
        if name in self._gp_dict:
            gp = self._gp_dict[name]
            assert isinstance(gp, GeneratorParam)
            old_value = gp._value
            new_value = GeneratorParam._parse_value(name, type(old_value), value)
            # Do not mutate the existing GP in place; it could be shared across multiple Generators.
            self._gp_dict[name] = GeneratorParam(new_value)
        elif name == "autoscheduler":
            _check(not self.autoscheduler().name, "The GeneratorParam %s cannot be set more than once" % name)
            self.autoscheduler().name = value
        elif name.startswith("autoscheduler."):
            sub_key = name[len("autoscheduler."):]
            _check(sub_key not in self.autoscheduler().extra,
                   "The GeneratorParam %s cannot be set more than once" % name)
            self.autoscheduler().extra[sub_key] = value
        else:
            self._unhandled_generator_params[name] = value

    def _build_pipeline(self) -> Pipeline:
        self._advance_to_stage(_Stage.io_replaced)

        assert not self._pipeline
        assert not self._input_parameters
        assert not self._output_funcs

        # Ensure that the current context is the one in self.
        # For most Generators this won't matter, but if the Generator
        # invokes SomeOtherGenerator.call(), it would be nice to have this
        # be the default, so that the end user doesn't have to mess with it.
        with self.context():
            self.generate()

        self._input_parameters = {n: getattr(self, n).parameter() for n in self._inputs_dict}
        self._output_funcs = {n: getattr(self, n) for n in self._outputs_dict}
        _check(
            len(self._output_funcs) > 0,
            "Generator %s must declare at least one Output in Arguments." % self._get_name(),
        )

        funcs = []
        for name, f in self._output_funcs.items():
            _check(f.defined(), "Output '%s' was not defined." % name)
            self._requirements[name]._check_types_and_dimensions(f.types(), f.dimensions())
            funcs.append(f)

        self._pipeline = Pipeline(funcs)
        for condition, error_args in self._pipeline_requirements:
            self._pipeline.add_requirement(condition, *error_args)
        self._stage = _Stage.pipeline_built
        return self._pipeline

    def _get_input_parameter(self, name: str) -> InternalParameter:
        assert self._stage == _Stage.pipeline_built
        _check(name in self._input_parameters, "Unknown input: %s" % name)
        return self._input_parameters[name]

    def _get_output_func(self, name: str) -> Func:
        assert self._stage == _Stage.pipeline_built
        _check(name in self._output_funcs, "Unknown output: %s" % name)
        return self._output_funcs[name]

    def _bind_input(self, name: str, values: list[object]):
        assert self._stage < _Stage.io_replaced
        self._advance_to_stage(_Stage.configure_called)
        _check(len(values) == 1, "Too many values specified for input: %s" % name)
        _check(name in self._inputs_dict, "There is no input with the name: %s" % name)
        assert name not in self._replacements
        self._replacements[name] = self._inputs_dict[name]._make_replacement(values[0], self._requirements[name])


_python_generators: dict = {}


def _get_python_generator_names() -> list[str]:
    return _python_generators.keys()


def _create_python_generator(name: str, context: GeneratorContext):
    cls = _python_generators.get(name, None)
    if not isclass(cls):
        return None
    with context:
        return cls()


def _fqname(o):
    k = o
    m = k.__module__
    q = k.__qualname__
    if m == "__main__" or k == "builtins":
        return q
    return m + "." + q


def active_generator_context() -> GeneratorContext:
    context = _halide_generator_context.get()
    _check(isinstance(context, GeneratorContext), "There is no active GeneratorContext")
    return context


def _is_interactive_mode() -> bool:
    return hasattr(sys, 'ps1')


def _check_generator_name_in_use(n:str):
    if _is_interactive_mode():
        # In interactive mode, it's OK to redefine generators... in fact, it's really
        # annoying not to allow this (e.g. in Colab)
        #
        # (Want to debug? Include this:)
        # if n in _python_generators:
        #     builtins.print("REDEFINING ", n)
        return

    _check(n not in _python_generators, "The Generator name %s is already in use." % n)


def alias(**kwargs):

    def alias_impl(cls):
        for k, v in kwargs.items():
            _check_valid_name(k)
            _check(hasattr(cls, "_halide_registered_name"), "@alias can only be used in conjunction with @generator.")
            _check_generator_name_in_use(k)
            _check(type(v) is dict, "The Generator alias %s specifies something other than a dict." % k)
            new_cls = type(k, (cls,), {"_halide_registered_name": k, "_halide_alias_generator_params": v})
            _python_generators[k] = new_cls
        return cls

    return alias_impl

def generator(name:str=""):
    # This code relies on dicts preserving key-insertion order, which is only
    # guaranteed for all Python implementations as of v3.7.
    _check(sys.version_info >= (3, 7), "Halide Generators require Python 3.7 or later.")
    def generator_impl(cls):
        n = name if name else _fqname(cls)
        _check_generator_name_in_use(n)
        _check(isclass(cls), "@generator can only be used on classes.")
        # Allow (but don't require) explicit inheritance from hl.Generator;
        # static type checkers (e.g. pytype) can complain that the decorated class
        # uses inherited methods since it can't correctly infer the inheritance.
        if issubclass(cls, Generator):
            new_cls = type(cls.__name__, (cls,), {"_halide_registered_name": n})
        else:
            new_cls = type(cls.__name__, (cls, Generator), {"_halide_registered_name": n})
        _python_generators[n] = new_cls
        return new_cls

    return generator_impl

def funcs(names:str) -> tuple(Func):
    """Given a space-delimited string, create a Func for each substring and return as a tuple."""
    return (Func(n) for n in names.split(' '))


def vars(names:str) -> tuple(Var):
    """Given a space-delimited string, create a Var for each substring and return as a tuple."""
    return (Var(n) for n in names.split(' '))
