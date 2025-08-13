import tempfile
from pathlib import Path

import halide as hl
import numpy as np


def test_serialize_deserialize_pipeline_file():
    """Test serializing and deserializing a pipeline to/from a file."""
    x, y = hl.vars("x y")
    f = hl.Func("f")
    f[x, y] = x + y

    pipeline = hl.Pipeline(f)

    with tempfile.NamedTemporaryFile(suffix=".hlpipe", delete=False) as tmp:
        filename = Path(tmp.name)

    try:
        hl.serialize_pipeline(pipeline, filename)

        assert filename.exists()
        assert filename.stat().st_size > 0

        deserialized_pipeline = hl.deserialize_pipeline(filename)

        result = deserialized_pipeline.realize([10, 10])
        assert result.dim(0).extent() == 10
        assert result.dim(1).extent() == 10

        expected = np.add.outer(np.arange(10), np.arange(10))
        assert np.array_equal(np.array(result), expected)

    finally:
        filename.unlink(missing_ok=True)


def test_serialize_deserialize_pipeline_bytes():
    """Test serializing and deserializing a pipeline to/from bytes."""
    x, y = hl.Var("x"), hl.Var("y")
    f = hl.Func("f")
    f[x, y] = x * 2 + y

    pipeline = hl.Pipeline(f)

    data = hl.serialize_pipeline(pipeline)
    assert isinstance(data, bytes)
    assert len(data) > 0

    deserialized_pipeline = hl.deserialize_pipeline(data)

    result = deserialized_pipeline.realize([5, 5])
    expected = np.fromfunction(lambda x, y: x * 2 + y, (5, 5), dtype=int).transpose()
    assert np.array_equal(np.array(result), expected)


def test_serialize_deserialize_with_parameters():
    """Test serializing and deserializing a pipeline with external parameters."""
    x = hl.Var("x")
    p = hl.Param(hl.Int(32), "multiplier", 1)
    f = hl.Func("f")
    f[x] = x * p

    pipeline = hl.Pipeline(f)

    with tempfile.NamedTemporaryFile(suffix=".hlpipe", delete=False) as tmp:
        filename = Path(tmp.name)

    try:
        params = hl.serialize_pipeline(pipeline, filename, get_params=True)

        assert "multiplier" in params
        assert params["multiplier"].name() == "multiplier"

        user_params = {"multiplier": hl.Param(hl.Int(32), "multiplier", 5).parameter()}
        deserialized_pipeline = hl.deserialize_pipeline(filename, user_params)

        result = deserialized_pipeline.realize([3])
        assert list(result) == [0, 5, 10]

    finally:
        filename.unlink(missing_ok=True)


def test_serialize_deserialize_with_parameters_bytes():
    """Test serializing and deserializing a pipeline with parameters using bytes."""
    x = hl.Var("x")
    p = hl.Param(hl.Int(32), "offset", 0)
    f = hl.Func("f")
    f[x] = x + p

    pipeline = hl.Pipeline(f)

    data, params = hl.serialize_pipeline(pipeline, get_params=True)

    assert "offset" in params
    assert params["offset"].name() == "offset"

    user_params = {"offset": hl.Param(hl.Int(32), "offset", 100).parameter()}
    deserialized_pipeline = hl.deserialize_pipeline(data, user_params)

    result = deserialized_pipeline.realize([3])
    assert list(result) == [100, 101, 102]


def test_deserialize_parameters_file():
    """Test deserializing just the parameters from a file."""
    x = hl.Var("x")
    p1 = hl.Param(hl.Int(32), "param1", 1)
    p2 = hl.Param(hl.Float(32), "param2", 2.0)
    f = hl.Func("f")
    f[x] = hl.cast(hl.Int(32), x * p1 + p2)

    pipeline = hl.Pipeline(f)

    with tempfile.NamedTemporaryFile(suffix=".hlpipe", delete=False) as tmp:
        filename = Path(tmp.name)

    try:
        hl.serialize_pipeline(pipeline, filename)
        params = hl.deserialize_parameters(filename)

        assert "param1" in params
        assert "param2" in params
        assert params["param1"].name() == "param1"
        assert params["param2"].name() == "param2"
        assert params["param1"].type() == hl.Int(32)
        assert params["param2"].type() == hl.Float(32)

    finally:
        filename.unlink(missing_ok=True)


def test_deserialize_parameters_bytes():
    """Test deserializing just the parameters from bytes."""
    x = hl.Var("x")
    p1 = hl.Param(hl.UInt(16), "width", 64)
    p2 = hl.Param(hl.UInt(16), "height", 64)
    f = hl.Func("f")
    f[x] = hl.select(x < p1, p2, 0)

    pipeline = hl.Pipeline(f)

    data = hl.serialize_pipeline(pipeline)
    params = hl.deserialize_parameters(data)

    assert "width" in params
    assert "height" in params
    assert params["width"].type() == hl.UInt(16)
    assert params["height"].type() == hl.UInt(16)


def test_pipeline_with_multiple_outputs():
    """Test serializing/deserializing a pipeline with multiple outputs."""
    x, y = hl.vars("x y")

    f1 = hl.Func("f1")
    f1[x, y] = x + y

    f2 = hl.Func("f2")
    f2[x, y] = x - y

    pipeline = hl.Pipeline([f1, f2])

    data = hl.serialize_pipeline(pipeline)
    deserialized_pipeline = hl.deserialize_pipeline(data)

    results = deserialized_pipeline.realize([5, 5])
    assert len(results) == 2

    expected = np.add.outer(np.arange(5), np.arange(5))
    assert np.array_equal(np.array(results[0]), expected)

    expected = np.subtract.outer(np.arange(5), np.arange(5)).transpose()
    assert np.array_equal(np.array(results[1]), expected)


def test_empty_user_params():
    """Test that empty user_params works correctly."""
    x = hl.Var("x")
    f = hl.Func("f")
    f[x] = x * x

    pipeline = hl.Pipeline(f)
    data = hl.serialize_pipeline(pipeline)

    deserialized1 = hl.deserialize_pipeline(data, {})
    result1 = deserialized1.realize([3])
    assert list(result1) == [0, 1, 4]

    deserialized2 = hl.deserialize_pipeline(data)
    result2 = deserialized2.realize([3])
    assert list(result2) == [0, 1, 4]


if __name__ == "__main__":
    test_serialize_deserialize_pipeline_file()
    test_serialize_deserialize_pipeline_bytes()
    test_serialize_deserialize_with_parameters()
    test_serialize_deserialize_with_parameters_bytes()
    test_deserialize_parameters_file()
    test_deserialize_parameters_bytes()
    test_pipeline_with_multiple_outputs()
    test_empty_user_params()
    print("Success!")
