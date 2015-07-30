for f in ../lesson_*.cpp; do
    # Figure out which source lines contain a realize call
    LINES=$(egrep -n "[.]realize|[.]print_loop_nest" $f  | cut -d':' -f1)

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
    echo "set args -s 2>stderr.txt" >> gdb_script.txt
    echo start >> gdb_script.txt
    for l in $LINES; do
        echo advance ${FILENAME}:${l} >> gdb_script.txt
        echo call "fprintf(stderr, \"BEGIN_REALIZE_${l}_\\n\")" >> gdb_script.txt
        echo next >> gdb_script.txt
        echo call "fprintf(stderr, \"END_REALIZE_${l}_\\n\")" >> gdb_script.txt
    done

    cd ..
    LD_LIBRARY_PATH=../bin gdb ../bin/tutorial_${LESSON} < figures/gdb_script.txt
    mv stderr.txt figures/
    cd figures

    # get the output for each realize call
    rm -f ${LESSON_ROOT}_output_*.txt
    for l in $LINES; do
        F=${LESSON_ROOT}_output_${l}.txt
        sed -n "/BEGIN_REALIZE_${l}_/,/END_REALIZE_${l}_/p" < stderr.txt | grep -v _REALIZE > ${F}
        # delete the empty ones
        if [ ! -s ${F} ]; then
            rm ${F}
        fi
    done
done




#rm stderr.txt gdb_script.txt