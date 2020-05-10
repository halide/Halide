# Rerun the cases where the runtime ratio > 10% in either direction to filter out flakes. Must be run after generate_results.sh

cat ratios.csv  | grep -v names | cut -d, -f1,2 | grep -v ',1[.]0' | grep -v ',[.]9' | cut -d, -f1 | rev | sed 's/_/ /' | rev | while read app seed; do
    pushd ../${app}
    ../super_simplify/run_experiment.sh $seed $((seed + 1))
    popd
done

