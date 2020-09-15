for f in ../lesson_*.cpp; do
    # Figure out which source lines contain a realize call or other print we want to capture
    LINES=$(egrep -n 'tick\(|[.]realize\(|print_loop_nest\(\)|Printing a complex Expr' $f  | grep -v ": *//" | cut -d':' -f1)

    echo $LINES

    FILENAME=${f/../tutorial}
    LESSON=$(echo $f | sed "s/.*\(lesson.*\).cpp/\1/")
    LESSON_ROOT=$(echo $f | sed "s/.*\(lesson_..\).*/\1/")
    BINARY=../../bin/tutorial_${LESSON}

    rm -f ${BINARY}
    make -C ../.. bin/tutorial_${LESSON} OPTIMIZE=-g

    echo $FILENAME $BINARY $LESSON_ROOT

    # make a gdb script that captures stderr for each realize call
    rm -f gdb_script.txt
    rm -f stderr.txt
    touch gdb_script.txt
    echo "set args -s 2> stderr.txt > stdout.txt" >> gdb_script.txt
    echo "set height 0" >> gdb_script.txt
    echo "start" >> gdb_script.txt
    for l in $LINES; do
        echo advance ${FILENAME}:${l} >> gdb_script.txt
        echo call "fflush(stdout)" >> gdb_script.txt
        echo call "fflush(stderr)" >> gdb_script.txt
        echo call "fprintf(stderr, \"BEGIN_REALIZE_${l}_\\n\")" >> gdb_script.txt
        echo call "fprintf(stdout, \"BEGIN_REALIZE_${l}_\\n\")" >> gdb_script.txt
        echo call "fflush(stdout)" >> gdb_script.txt
        echo call "fflush(stderr)" >> gdb_script.txt
        echo next >> gdb_script.txt
        echo call "usleep(1000)" >> gdb_script.txt
        echo call "fprintf(stderr, \"END_REALIZE_${l}_\\n\")" >> gdb_script.txt
        echo call "fprintf(stdout, \"END_REALIZE_${l}_\\n\")" >> gdb_script.txt
        echo call "fflush(stdout)" >> gdb_script.txt
        echo call "fflush(stderr)" >> gdb_script.txt
    done


    cd ..
    LD_LIBRARY_PATH=../bin gdb ../bin/tutorial_${LESSON} < figures/gdb_script.txt
    mv stdout.txt stderr.txt figures/
    cd figures

    # get the output for each realize call
    rm -f ${LESSON_ROOT}_output_*.txt
    for l in $LINES; do
        F=${LESSON_ROOT}_output_${l}.txt
        cat stdout.txt | sed -n "/BEGIN_REALIZE_${l}_/,/END_REALIZE_${l}_/p" | grep -v _REALIZE > ${F}
        cat stderr.txt | sed -n "/BEGIN_REALIZE_${l}_/,/END_REALIZE_${l}_/p" | grep -v _REALIZE >> ${F}
        # delete the empty ones
        if [ ! -s ${F} ]; then
            rm ${F}
        fi
    done
done

# For lesson 3, we want the HL_DEBUG_CODEGEN output for the first
# mention of that
L=$(grep -n HL_DEBUG_CODEGEN ../lesson_03_*.cpp | head -n1 | cut -d':' -f1)
HL_DEBUG_CODEGEN=1 make -C ../.. tutorial_lesson_03* OPTIMIZE=-g 2> lesson_03_output_${L}.txt

#rm stderr.txt gdb_script.txt
