#!/bin/bash
# This script combines a bunch of static libs and does a partial link across all object
# files to resolve all internal references to produce a merged static lib.

# Check if at least two arguments are provided (input libraries + output library)
if [ "$#" -lt 2 ]; then
    echo "Usage: $0 --ld <path-to-ld> --ar <path-to-ar> <output.a> <input1.a> [input2.a ...]"
    exit 1
fi

POSITIONAL_ARGS=()
LD_TOOL="ld"
AR_TOOL="ar"
OUTPUT_LIB=""

while [[ $# -gt 0 ]]; do
  case $1 in
    -l|--ld)
      LD_TOOL="$2"
      shift # past argument
      shift # past value
      ;;
    -a|--ar)
      AR_TOOL="$2"
      shift # past argument
      shift # past value
      ;;
    -o|--output)
      OUTPUT_LIB="$2"
      shift # past argument
      shift # past value
      ;;
    -*|--*)
      echo "Unknown option $1"
      exit 1
      ;;
    *)
      POSITIONAL_ARGS+=("$1") # save positional arg
      shift # past argument
      ;;
  esac
done

set -- "${POSITIONAL_ARGS[@]}" # restore positional parameters

OS=$(uname -s)
OS="`uname`"
case $OS in
  'Linux')
    LD_FLAGS="-Ur"
    ;;
  'Darwin') 
    LD_FLAGS="-r"
    ;;
  *) ;;
esac

echo "Creating merged library $OUTPUT_LIB ..."
OUTPUT_OBJ="${OUTPUT_LIB%.*}.o"

# Create a temporary directory to extract object files
TEMP_DIR=$(mktemp -d)
echo "Creating temporary directory $TEMP_DIR ..."
if [ $? -ne 0 ]; then
    echo "Error: Could not create temporary directory."
    exit 1
fi
cd $TEMP_DIR

# Process each input library
MERGE_OBJS=()
for LIB in "$@"; do
    if [ ! -f "$LIB" ]; then
        echo "Error: Input library '$LIB' not found."
        rm -rf "$TEMP_DIR"
        exit 1
    fi
    echo "Extracting objects from $LIB ..."
    LIB_NAME="$(basename $LIB)"

    # Extract object files from each input library into 
    # a subdir in the temporary directory
    mkdir "$TEMP_DIR/$LIB_NAME"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create dir $TEMP_DIR/$LIB_NAME."
        exit 1
    fi

    cd "$TEMP_DIR/$LIB_NAME"
    $AR_TOOL -x "$LIB"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to extract objects from $LIB."
        exit 1
    fi

    # Rename the library object files and prepend the library name 
    # to the object file name in the top level temporary directory
    cd "$TEMP_DIR"
    for OBJ_FILE in "$LIB_NAME"/*.o; do
        OBJ_NAME="$(basename $OBJ_FILE)"
        mv "$OBJ_FILE" "$TEMP_DIR/$LIB_NAME-$OBJ_NAME"
        MERGE_OBJS+=("$TEMP_DIR/$LIB_NAME-$OBJ_NAME")
    done
    if [ $? -ne 0 ]; then
        echo "Error: Failed to extract objects from $LIB."
        exit 1
    fi
done

# Do a partial-link from all extracted object files to resolve internal references
cd "$TEMP_DIR"
echo "Creating merged object file $OUTPUT_OBJ ..."
$LD_TOOL "$LD_FLAGS" -o "$OUTPUT_OBJ" "${MERGE_OBJS[@]}"
if [ $? -ne 0 ]; then
    echo "Error: Failed to create the merged library."
    rm -r "$TEMP_DIR"
    exit 1
fi

# Create the final static lib
echo "Creating merged library $OUTPUT_LIB ..."
$AR_TOOL -rcsv "$OUTPUT_LIB" "$OUTPUT_OBJ"
if [ $? -ne 0 ]; then
    echo "Error: Failed to create the merged library."
    rm -r "$TEMP_DIR"
    exit 1
fi

# Clean up the temporary directory
echo "Cleaning up temporary directory..."
rm -r "$TEMP_DIR"

echo "Done!"