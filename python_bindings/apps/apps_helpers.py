import os

# Return the directory to look in for test images:
# - If TEST_IMAGES_DIR is defined, use that
# - Otherwise, create a relative path to the C++ apps/images dir
def apps_images_dir():
    return os.environ.get("TEST_IMAGES_DIR", os.path.join(os.path.dirname(__file__), "../../apps/images"))

# Return the directory to use when writing output files:
# - If TEST_TMPDIR is defined, use that
# - Otherwise, return an empty string (i.e., relative to whatever the current directory is)
def apps_output_dir():
    return os.environ.get("TEST_TMPDIR", "")
