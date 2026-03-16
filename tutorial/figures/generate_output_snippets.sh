#!/usr/bin/env bash

for f in ../lesson_*.cpp; do
    # Figure out which source lines contain a realize call or other print we want to capture
    INTERESTING_LINES=$(grep -En 'tick\(|[.]realize\(|print_loop_nest\(\)|Printing a complex Expr' "$f" | grep -v ": *//" | cut -d':' -f1)

    echo "$INTERESTING_LINES"

    FILENAME=${f/../tutorial}
    LESSON=${f##*/}
    LESSON=${LESSON%.cpp}
    LESSON_ROOT=${LESSON:0:9}
    BINARY=../../bin/tutorial_${LESSON}

    rm -f "${BINARY}"
    make -C ../.. bin/tutorial_"${LESSON}" OPTIMIZE=-g

    echo "$FILENAME" "$BINARY" "$LESSON_ROOT"

    # make a gdb script that captures stderr for each realize call
    rm -f stderr.txt
    {
        echo "set args -s 2> stderr.txt > stdout.txt"
        echo "set height 0"
        echo "start"
    } >gdb_script.txt
    # shellcheck disable=SC2129
    for l in ${INTERESTING_LINES}; do
        echo advance "${FILENAME}":"${l}" >>gdb_script.txt
        echo call "fflush(stdout)" >>gdb_script.txt
        echo call "fflush(stderr)" >>gdb_script.txt
        printf 'call "fprintf(stderr, \\"BEGIN_REALIZE_%s_\\\\n\\")"\n' "${l}" >>gdb_script.txt
        printf 'call "fprintf(stdout, \\"BEGIN_REALIZE_%s_\\\\n\\")"\n' "${l}" >>gdb_script.txt
        echo call "fflush(stdout)" >>gdb_script.txt
        echo call "fflush(stderr)" >>gdb_script.txt
        echo next >>gdb_script.txt
        echo call "usleep(1000)" >>gdb_script.txt
        printf 'call "fprintf(stderr, \\"END_REALIZE_%s_\\\\n\\")"\n' "${l}" >>gdb_script.txt
        printf 'call "fprintf(stdout, \\"END_REALIZE_%s_\\\\n\\")"\n' "${l}" >>gdb_script.txt
        echo call "fflush(stdout)" >>gdb_script.txt
        echo call "fflush(stderr)" >>gdb_script.txt
    done

    cd ..
    LD_LIBRARY_PATH=../bin gdb ../bin/tutorial_"${LESSON}" <figures/gdb_script.txt
    mv stdout.txt stderr.txt figures/
    cd figures || exit

    # get the output for each realize call
    rm -f "${LESSON_ROOT}"_output_*.txt
    for l in ${INTERESTING_LINES}; do
        F=${LESSON_ROOT}_output_${l}.txt
        cat stdout.txt | sed -n "/BEGIN_REALIZE_${l}_/,/END_REALIZE_${l}_/p" | grep -v _REALIZE >"${F}"
        cat stderr.txt | sed -n "/BEGIN_REALIZE_${l}_/,/END_REALIZE_${l}_/p" | grep -v _REALIZE >>"${F}"
        # delete the empty ones
        if [ ! -s "${F}" ]; then
            rm "${F}"
        fi
    done
done

# For lesson 3, we want the HL_DEBUG_CODEGEN output for the first
# mention of that
L=$(grep -n HL_DEBUG_CODEGEN ../lesson_03_*.cpp | head -n1 | cut -d':' -f1)
HL_DEBUG_CODEGEN=1 make -C ../.. tutorial_lesson_03* OPTIMIZE=-g 2>lesson_03_output_"${L}".txt

#rm stderr.txt gdb_script.txt
