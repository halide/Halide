
make -C ../../ distrib -j16
make bin/random_pipeline.generator

for ((seed=0;seed<1024;seed++)); do
    bash ../../src/autoschedulers/adams2019/autotune_loop.sh bin/random_pipeline.generator random_pipeline x86-64-avx2-linux weights.data ../../distrib/bin ../../distrib samples 16 $seed seed=${seed}
done
