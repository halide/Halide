from abc import ABC, abstractmethod
from collections import OrderedDict
from enum import Enum
import sys

# Everything below here is implicitly in the `halide` package

def _fail(msg:str):
    raise RuntimeError(msg)

def _check(cond:bool, msg:str):
    if not cond:
        _fail(msg)

def _check_internal(cond:bool):
    _check(cond, "Internal Error")

class Proxy:
    def __init__(self, obj:object):
        self._wrapped = obj;

    def __getattr__(self, attr):
        if attr in self.__dict__:
            return getattr(self, attr)
        return getattr(self._wrapped, attr)

    def __getitem__(self, key):
        return self._wrapped.__getitem__(key)

    def __setitem__(self, key, value):
        self._wrapped.__setitem__(key, value)

    def __add__(self, other):
        return self._wrapped + other

    def __radd__(self, other):
        return other + self._wrapped

    def __sub__(self, other):
        return self._wrapped - other

    def __rsub__(self, other):
        return other - self._wrapped

    def __mul__(self, other):
        return self._wrapped * other

    def __rmul__(self, other):
        return other * self._wrapped

    def __div__(self, other):
        return self._wrapped / other

    def __rdiv__(self, other):
        return other / self._wrapped

    def __truediv__(self, other):
        return self._wrapped / other

    def __rtruediv__(self, other):
        return other / self._wrapped

    def __mod__(self, other):
        return self._wrapped % other

    def __rmod__(self, other):
        return other % self._wrapped

    def __lshift__(self, other):
        return self._wrapped << other

    def __rlshift__(self, other):
        return other << self._wrapped

    def __rshift__(self, other):
        return self._wrapped >> other

    def __rrshift__(self, other):
        return other >> self._wrapped

    def __and__(self, other):
        return self._wrapped & other

    def __rand__(self, other):
        return other & self._wrapped

    def __or__(self, other):
        return self._wrapped | other

    def __ror__(self, other):
        return other | self._wrapped

    def __xor__(self, other):
        return self._wrapped ^ other

    def __rxor__(self, other):
        return other ^ self._wrapped

    def __lt__(self, other):
        return self._wrapped < other

    def __rlt__(self, other):
        return other < self._wrapped

    def __le__(self, other):
        return self._wrapped <= other

    def __rle__(self, other):
        return other <= self._wrapped

    def __eq__(self, other):
        return self._wrapped == other

    def __req__(self, other):
        return other == self._wrapped

    def __ne__(self, other):
        return self._wrapped != other

    def __rne__(self, other):
        return other != self._wrapped

    def __ge__(self, other):
        return self._wrapped >= other

    def __rge__(self, other):
        return other >= self._wrapped

    def __gt__(self, other):
        return self._wrapped > other

    def __rgt__(self, other):
        return other > self._wrapped

    def __floordiv__(self, other):
        return self._wrapped // other

    def __rfloordiv__(self, other):
        return other // self._wrapped


def _normalize_type_list(types:object) -> list[Type]:
    if types is None:
        types = []
    elif isinstance(types, Type):
        types = [types]
    for t in types:
        _check(isinstance(t, Type), "List-of-Type contains non-type items %s" % t)
    return types


class GIOBase(Proxy):
    # Name
    _name:str = ""

    # List of the required types, if any. An empty list means
    # no constraints. The list will usually be a single type,
    # except for Outputs that have Tuple-valued results, which
    # can have multiple types.
    _required_types:list[Type] = []

    # Required dimensions. 0 = scalar. -1 = unconstrained.
    _required_dimensions:int = -1

    def __init__(self, name:str, value:object, types:object, dimensions:int):
        Proxy.__init__(self, value)
        self._name = name
        self._required_types = _normalize_type_list(types)
        self._required_dimensions = dimensions

    # TODO: enhance Proxy to allow autoconversion from e.g. GP -> LoopLevel
    def value(self):
        return self._wrapped

    def _check_required_types(self, types:list[Type]):
        _check(len(self._required_types) > 0, "No type has been specified for %s. Try specifying one in code, or by setting '%s.type' as a GeneratorParam." % (self._name, self._name))
        types = _normalize_type_list(types)
        _check_internal(len(types) > 0)
        _check(len(types) == len(self._required_types), "Type mismatch for %s: expected %d types but saw %d" % (self._name, len(self._required_types), len(types)))
        for i in range(0, len(types)):
            _check(self._required_types[i] == types[i], "Type mismatch for %s: expected %s saw %s" % (self._name, self._required_types[i], types[i]))


    def _check_required_dimensions(self, dims:int):
        _check(self._required_dimensions >= 0, "No dimensionality has been specified for %s. Try specifying one in code, or by setting '%s.dim' as a GeneratorParam." % (self._name, self._name))
        _check_internal(dims >= 0)
        _check(dims == self._required_dimensions, "Dimensions mismatch for %s: expected %d saw %d" % (self._name, self._required_dimensions, dims))

    def _set_required_types(self, required_types):
        required_types = _normalize_type_list(required_types)
        _check(len(required_types) > 0, "Cannot set the GeneratorParam %s to unspecified type." % self._name)
        _check(len(self._required_types) == 0,
                "Cannot set the GeneratorParam %s because the value is explicitly specified in the Python source." % self._name)
        self._required_types = _normalize_type_list(required_types)
        self._wrapped = self._build_wrapped(self._name, self._required_types, self._required_dimensions)

    def _set_required_dimensions(self, required_dimensions):
        _check(required_dimensions >= 0, "Cannot set the GeneratorParam %s to unspecified dimensions." % self._name)
        _check(self._required_dimensions == -1,
                "Cannot set the GeneratorParam %s because the value is explicitly specified in the Python source." % self._name)
        self._required_dimensions = required_dimensions
        assert self._required_dimensions >= 0
        self._wrapped = self._build_wrapped(self._name, self._required_types, self._required_dimensions)

    def _is_generatorparam(self):
        return False

    def _is_input(self):
        return False

    def _is_output(self):
        return False

    def _to_arginfo(self):
        return None

    def _to_parameter(self):
        return None

    def _to_output_func(self):
        return None

    def _to_generator_param(self):
        return None

    def _set_value(self, value:object):
        _fail("Internal Error")

    @staticmethod
    def _build_wrapped(name:str, types:object, dimensions:int):
        _fail("Internal Error")

    @staticmethod
    def _is_valid_name(name:str) -> bool:
        # Basically, a valid C identifier, except:
        #
        # -- initial _ is forbidden (rather than merely "reserved")
        # -- two underscores in a row is also forbidden
        if not name:
            return False
        if "__" in name:
            return False
        if not name[0].isalpha():
            return False
        for c in name[1:]:
            if not (c.isalnum() or c == '_'):
                return False
        return True

_type_string_map = {
    "bool": Bool(),
    "int8": Int(8),
    "int16": Int(16),
    "int32": Int(32),
    "uint8": UInt(8),
    "uint16": UInt(16),
    "uint32": UInt(32),
    "float16": Float(16),
    "float32": Float(32),
    "float64": Float(64)
}

def _parse_halide_type(s:str) -> Type:
    _check(s in _type_string_map, "The value %s cannot be parsed as a Type." % s)
    return _type_string_map[s]

def _parse_halide_type_list(s:str) -> list[Type]:
    return [_parse_halide_type(t) for t in s.split(',')]

# GeneratorParam is a string, int, float, bool, Type, or LoopLevel
class GeneratorParam(GIOBase):
    def __init__(self, default_value, name:str = ""):
        GIOBase.__init__(self, name, default_value, None, -1)
        self._set_value(default_value)

    # TODO: add for all GIO items
    def __repr__(self):
        if isinstance(self._wrapped, LoopLevel):
            return "GeneratorParam(name='%s', value=[LoopLevel])" % self._name
        else:
            return "GeneratorParam(name='%s', value=%s)" % (self._name, repr(self._wrapped))

    def _is_generatorparam(self):
        return True

    @staticmethod
    def _parse_gp_value(gp_type:type, value:object) -> object:
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
        elif gp_type is LoopLevel:
            if value == "root":
                value = LoopLevel.root()
            elif value == "inlined":
                value = LoopLevel.inlined()
            if not isinstance(value, LoopLevel):
                # Don't attempt to examine value unless we know it is NOT a LoopLevel
                _fail("The value %s cannot be parsed as a LoopLevel." % str(value))
            return value
        else:
            _fail("GeneratorParam %s has unsupported type %s" % (self._name, gp_type))

    def _set_value(self, value):
        self._wrapped = self._parse_gp_value(type(self._wrapped), value)

    def _clone_with_default_name(self, name:str):
        if self._name:
            name = self._name
        return GeneratorParam(self._wrapped, name)

    def _to_generator_param(self):
        return self

class SyntheticGeneratorParamKind(Enum):
    Type = 1
    Dimensions = 2

# A 'fake' GeneratorParam that allow setting of an Input/Output's type or dim,
# but only if that type or dim is unspecified in the source code
class SyntheticGeneratorParam(GeneratorParam):
    def __init__(self, name:str, generator_name:str, linked_gio:GIOBase, kind:SyntheticGeneratorParamKind):
        default_value = None  # Won't be used
        GeneratorParam.__init__(self, default_value, name)
        self._generator_name = generator_name
        self._linked_gio = linked_gio
        self._kind = kind

    def _clone_with_default_name(self, name:str):
        if self._name:
            name = self._name
        return SyntheticGeneratorParam(name, self._generator_name, self._linked_gio, self._kind)

    def _set_value(self, value):
        # No: don't call the super() method here.
        # super()._set_value(value)
        if not hasattr(self, "_kind"):
            return

        g = self._linked_gio
        if self._kind == SyntheticGeneratorParamKind.Type:
            value = self._parse_gp_value(Type, value)
            g._set_required_types(value)
        elif self._kind == SyntheticGeneratorParamKind.Dimensions:
            value = self._parse_gp_value(int, value)
            _check(g._required_dimensions == -1,
                    "Cannot set the GeneratorParam %s for %s because the value is explicitly specified in the Python source." % (self._name, self._generator_name))
            g._set_required_dimensions(value)


class InputBase(GIOBase):
    def __init__(self, name:str, type:Type, dimensions:int):
        GIOBase.__init__(self, name, self._build_wrapped(name, type, dimensions), type, dimensions)

    def _is_input(self):
        return True


# InputBuffer looks like an ImageParam
class InputBuffer(InputBase):
    def __init__(self, type:Type, dimensions:int, name:str = ""):
        InputBase.__init__(self, name, type, dimensions)

    @staticmethod
    def _build_wrapped(name:str, types:object, dimensions:int):
        types = _normalize_type_list(types)
        if name and len(types) == 1 and dimensions >= 0:
            return ImageParam(types[0], dimensions, name)
        else:
            return ImageParam()

    def _clone_with_default_name(self, name:str):
        if self._name:
            name = self._name
        return InputBuffer(self._required_types, self._required_dimensions, name)

    def _to_arginfo(self):
        return ArgInfo(self._name, ArgInfoDir.Input, ArgInfoKind.Buffer, self._required_types, self._required_dimensions)

    def _to_parameter(self):
        _check_internal(self._wrapped.defined())
        return self._wrapped.parameter()

    def _set_value(self, value:object):
        _check(isinstance(value, (Buffer, ImageParam)),
                "InputBuffer %s requires an ImageParam or Buffer argument when calling apply(), but saw %s" % (self._name, str(value)))

        _check(value.defined(), "Cannot set the value for %s to an undefined value." % self._name)
        if isinstance(value, Buffer):
            im = ImageParam(value.type(), value.dimensions(), self._name)
            im.set(value)
            value = im
        self._check_required_types(value.type())
        self._check_required_dimensions(value.dimensions())
        self._wrapped = value


# InputScalar looks like a Param
class InputScalar(InputBase):
    def __init__(self, type:Type, name:str = ""):
        InputBase.__init__(self, name, type, 0)

    @staticmethod
    def _build_wrapped(name:str, types:object, dimensions:int):
        types = _normalize_type_list(types)
        if name and len(types) == 1 and dimensions == 0:
            return Param(types[0], name)
        else:
            return Param(Handle(), "__illegal")

    def _clone_with_default_name(self, name:str):
        if self._name:
            name = self._name
        return InputScalar(self._required_types, name)

    def _to_arginfo(self):
        return ArgInfo(self._name, ArgInfoDir.Input, ArgInfoKind.Scalar, self._required_types, 0)

    def _to_parameter(self):
        # _check_internal(self._wrapped.defined()) -- Param<> is *always* defined. Check for illegal-to-user name instead.
        _check_internal(self._wrapped.name() != "__illegal")
        return self._wrapped.parameter()

    def _set_value(self, value:object):
        if isinstance(value, (int, float, bool)):
            self._wrapped.set(value)
        else:
            _check(isinstance(value, Param), "InputScalar requires a Param (or scalar literal) argument when calling apply(). " + str(value))
            _check(value.defined(), "Cannot set the value for %s to an undefined value." % self._name)
            self._check_required_types(value.type())
            self._check_required_dimensions(0)
            self._wrapped = value


# InputFunc looks like a Func, but requires type and dimension
class InputFunc(InputBase):
    def __init__(self, type:Type, dimensions:int, name:str = ""):
        InputBase.__init__(self, name, type, dimensions)

    def type(self) -> Type:
        _check(len(self._required_types) == 1, "InputFunc %s must have exactly one type specified in order to call the .type() method." % self._name)
        return self._required_types[0]

    @staticmethod
    def _build_wrapped(name:str, types:object, dimensions:int):
        # types = _normalize_type_list(types)  -- unnecessary here
        if name:
            return Func(name)
        else:
            return Func()

    def _clone_with_default_name(self, name:str):
        if self._name:
            name = self._name
        return InputFunc(self._required_types, self._required_dimensions, name)

    def _to_arginfo(self):
        return ArgInfo(self._name, ArgInfoDir.Input, ArgInfoKind.Function, self._required_types, self._required_dimensions)

    def _to_parameter(self):
        _check_internal(self._wrapped.defined())
        # TODO: gross hack, use an ImageParam to construct a Parameter for us
        return ImageParam(self._wrapped.output_types()[0], self._wrapped.dimensions(), self._name).parameter()

    def _set_value(self, value:object):
        # If passed an ImageParam, quietly extract its Func
        if isinstance(value, ImageParam):
            value = value.func()
        _check(isinstance(value, Func), "InputFunc %s requires a Func argument when calling apply(), but saw: %s" % (self._name, str(value)))
        _check(value.defined(), "Cannot set the value for %s to an undefined value." % self._name)
        self._check_required_types(value.output_types())
        self._check_required_dimensions(value.dimensions())
        self._wrapped = value


class OutputBase(GIOBase):
    def __init__(self, name:str, type:Type, dimensions:int):
        GIOBase.__init__(self, name, self._build_wrapped(name, type, dimensions), type, dimensions)

    def type(self) -> Type:
        _check(len(self._required_types) == 1, "Output %s must have exactly one type specified in order to call the .type() method." % self._name)
        return self._required_types[0]

    def _is_output(self):
        return True

    @staticmethod
    def _build_wrapped(name:str, types:object, dimensions:int):
        # types = _normalize_type_list(types)  -- unnecessary here
        if name:
            return Func(name)
        else:
            return Func()

    def _clone_with_default_name(self, name:str):
        if self._name:
            name = self._name
        return self.__class__(self._required_types, self._required_dimensions, name)

    def _arg_info_kind(self):
        return None

    def _to_arginfo(self):
        return ArgInfo(self._name, ArgInfoDir.Output, self._arg_info_kind(), self._required_types, self._required_dimensions)

    def _to_output_func(self):
        return self._wrapped

    def _set_value(self, value:object):
        _check(isinstance(value, (Buffer, Func)), "Output %s requires a Func or Buffer argument when calling apply(), but saw %s" % (self._name, str(value)))
        _check(value.defined(), "Cannot set the value for %s to an undefined value." % self._name)
        self._check_required_types(value.type())
        self._check_required_dimensions(value.dimensions())

        if isinstance(value, Buffer):
            # Allow assignment from a Buffer<> to an Output<Buffer<>>;
            # this allows us to use a statically-compiled buffer inside a Generator
            # to assign to an output.
            f = Func(self._name)
            f[_] = value[_]
            value = f

        self._wrapped = value


# OutputFunc is just a Func
class OutputFunc(OutputBase):
    def __init__(self, type:Type, dimensions:int, name:str = ""):
        OutputBase.__init__(self, name, type, dimensions)

    def _arg_info_kind(self):
        return ArgInfoKind.Function

# OutputBuffer is just like OutputFunc, except for its ArgInfoKind
class OutputBuffer(OutputBase):
    def __init__(self, type:Type, dimensions:int, name:str = ""):
        OutputBase.__init__(self, name, type, dimensions)

    def _arg_info_kind(self):
        return ArgInfoKind.Buffer


def _items_with_gio_names(d):
    return [(k, v) for k, v in d.items() if isinstance(v, GIOBase)]

def _unsorted_cls_dir(cls):
    kv = []
    for base in cls.__bases__:
        kv.extend(_unsorted_cls_dir(base))

    kv.extend(_items_with_gio_names(cls.__dict__))
    return kv

def _unsorted_dir(self):
    kv = _unsorted_cls_dir(self.__class__)
    kv.extend(_items_with_gio_names(self.__dict__))
    return kv

class Generator(ABC):
    """Base class for Halide Generators in Python"""
    def context(self):
        return self._context

    def get_target(self):
        return self.context().get_target()

    def get_auto_schedule(self):
        return self.context().get_auto_schedule()

    def get_machine_params(self):
        return self.context().get_machine_params()

    def natural_vector_size(self, type:Type) -> int:
        return self.get_target().natural_vector_size(type)

    def __setattr__(self, name, value):
        if name in self.__dict__:
            g = self.__dict__[name]
            # If self._param_info is None, we are still doing
            # initialization calls to setattr()
            if isinstance(g, GIOBase) and self._param_info:
                if g._is_output():
                    g._set_value(value)
                else:
                    _fail("You cannot overwrite Input or GeneratorParam fields ('%s')" % name)
        super().__setattr__(name, value)

    # Inputs can be specified by either positional or named args,
    # but may not be mixed. (i.e., if any inputs are specified as a named
    # argument, they all must be specified that way; otherwise they must all be
    # positional, in the order declared in the Generator.)
    #
    # GeneratorParams can only be specified by name, and are always optional.
    @classmethod
    def apply(cls, context, *args, **kwargs):
        # Allow passing a Target or a GeneratorContext
        if isinstance(context, Target):
            context = GeneratorContext(context)
        generator = cls(context)

        arginfos = generator._get_arginfos()
        input_arginfos = [a for a in arginfos if a.dir == ArgInfoDir.Input]
        output_arginfos = [a for a in arginfos if a.dir == ArgInfoDir.Output]

        input_names = [a.name for a in input_arginfos]

        # Process the kwargs first: first, fill in all the GeneratorParams
        # (in case some are tied to Inputs)
        for k, v in kwargs.items():
            if not k in input_names:
                # Allow synthetic params to be specified as __type or __dim
                k = k.replace("__type", ".type").replace("__dim", ".dim")
                generator._set_generatorparam_value(k, v)

        # Now inputs:
        kw_inputs_specified = 0
        for k, v in kwargs.items():
            if k in input_names:
                generator._bind_input(k, [v])
                kw_inputs_specified = kw_inputs_specified + 1

        if len(args) == 0:
            # No args specified positionally, so they must all be via keywords.
            _check(kw_inputs_specified == len(input_arginfos), "Expected exactly %d keyword args for inputs, but saw %d." % (len(input_arginfos), kw_inputs_specified))
        else:
            # Some positional args, so all inputs must be positional (and none via keyword).
            _check(kw_inputs_specified == 0, "Cannot use both positional and keyword arguments for inputs.")
            _check(len(args) == len(input_arginfos), "Expected exactly %d positional args for inputs, but saw %d." % (len(input_arginfos), len(args)))
            for i in range(0, len(args)):
                a = input_arginfos[i]
                k = a.name
                v = args[i]
                generator._bind_input(k, [v])

        generator._build_pipeline()

        outputs = []
        for o in output_arginfos:
            outputs.extend(generator._get_funcs_for_output(o.name))

        if len(outputs) == 1:
            return outputs[0]

        return tuple(outputs)

    def __init__(self, context:GeneratorContext):
        # TODO: use OrderedDict here?
        # self.__dict__ = OrderedDict()
        _check_internal(isinstance(context, GeneratorContext))
        self._context = context
        self._param_info = None
        self._pipeline = None
        self._input_parameters = None
        self._output_funcs = None

    @abstractmethod
    def generate(self):
        pass

    def schedule(self):
        pass

    def _add_synthetic_generator_param(self, linked_gio:GIOBase) -> list[SyntheticGeneratorParam]:
        if linked_gio._is_generatorparam():
            return []

        # Note that we create these for all relevant Inputs and Outputs, even though
        # some will be guaranteed to fail if you attempt to set them: that's the point,
        # we *want* things to fail if you try to set them inappopriately.
        type_sp = SyntheticGeneratorParam("%s.type" % linked_gio._name, self._get_name(), linked_gio, SyntheticGeneratorParamKind.Type)
        dim_sp = SyntheticGeneratorParam("%s.dim" % linked_gio._name, self._get_name(), linked_gio, SyntheticGeneratorParamKind.Dimensions)
        return [type_sp, dim_sp]

    def _get_param_info(self):
        if not self._param_info:
            pi = OrderedDict()
            for k, v in _unsorted_dir(self):
                if isinstance(v, GIOBase):
                    g =  v._clone_with_default_name(k)
                    pi[k] = g
                    _check(GIOBase._is_valid_name(g._name), "The name '%s' is not valid for a Generator member." % g._name)

            # Re-set all of these in the instance itself, since
            # (1) they will be different objects due to _clone_with_default_name
            # (2) this avoids sharing the class-member items in the unlikely event
            # that we instantiate multiple instances of the same Generator class at once,
            # but (more importantly) in the case where we call .apply() multiple times
            for k, v in pi.items():
                setattr(self, k, v)

            # Now add the synthetic GeneratorParams, but *don't* set them in the instance:
            # they are visible to the external build system interface, but not to the Generator
            # instance itself.
            sp = []
            for k, v in pi.items():
                sp.extend(self._add_synthetic_generator_param(v))

            for g in sp:
                assert not g._name in pi
                pi[g._name] = g

            # Set this last, so that our __setattr__ can check this for special casing
            self._param_info = pi;

        return self._param_info

    def _get_input_parameters(self):
        return {g._name:a for g, a in ((g, g._to_parameter()) for g in self._get_param_info().values()) if a is not None}

    def _get_output_funcs(self):
        return {g._name:a for g, a in ((g, g._to_output_func()) for g in self._get_param_info().values()) if a is not None}

    def _get_generator_params(self):
        return {g._name:a for g, a in ((g, g._to_generator_param()) for g in self._get_param_info().values()) if a is not None}

    # --------------- AbstractGenerator methods (partial)
    def _get_name(self) -> str:
        return self._registered_name

    def _get_arginfos(self):
        return [a for a in (g._to_arginfo() for g in self._get_param_info().values()) if a is not None]

    def _set_generatorparam_value(self, name:str, value:object):
        _check_internal(self._pipeline == None)
        gp = self._get_generator_params()
        _check(name in gp, "zzz Generator %s has no GeneratorParam named: %s" % (self._get_name(), name))
        gp[name]._set_value(value)

    def _build_pipeline(self) -> Pipeline:
        _check_internal(not self._pipeline and not self._input_parameters and not self._output_funcs)

        # It is critical that _get_param_info() is called *before* generate().
        param_info = self._get_param_info()

        self.generate()
        self.schedule()

        self._input_parameters = self._get_input_parameters()
        self._output_funcs = self._get_output_funcs()
        _check(len(self._output_funcs) > 0, "Generator %s must declare at least one Output in Arguments." % self._get_name())

        funcs = []
        for name, f in self._output_funcs.items():
            _check(f.defined(), "Output '%s' was not defined." % name)
            param_info[name]._check_required_types(f.output_types())
            param_info[name]._check_required_dimensions(f.dimensions())
            funcs.append(f)

        self._pipeline = Pipeline(funcs)
        return self._pipeline;

    def _get_parameters_for_input(self, name: str) -> list[InternalParameter]:
        _check_internal(self._input_parameters != None)
        _check(name in self._input_parameters, "Unknown input: %s" % name)
        return [self._input_parameters[name]]

    def _get_funcs_for_output(self, name: str) -> list[Func]:
        _check_internal(self._output_funcs != None)
        _check(name in self._output_funcs, "Unknown output: %s" % name)
        return [self._output_funcs[name]]

    def _bind_input(self, name:str, values: list[object]):
        param_info = self._get_param_info()
        _check(name in param_info, "There is no input with the name: %s" % name)
        _check(len(values) == 1, "Too many values specified for input: %s" % name)
        param_info[name]._set_value(values[0])

_python_generators:dict = {}

def _get_python_generator_names():
    return _python_generators.keys()

def _find_python_generator(name):
    if not name in _python_generators:
        return None
    return _python_generators[name]["class"]

def generator(name, other = []):
    def real_decorator(cls):
        _check(not issubclass(cls, Generator), "Please use the @hl.generator decorator instead of inheriting from hl.Generator")
        new_cls = type(cls.__name__, (cls, Generator), {})
        new_cls._registered_name = name
        _python_generators[name] = { "class": new_cls, "other": other }
        return new_cls

    return real_decorator
