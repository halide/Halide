The weights in this directory were trained in a hold-one-out fashion. Each
app was trained on a set of random pipelines and all the other apps, but not
itself. For example, bilateral_grid.weights was trained on the random pipelines
and all apps except bilateral grid.

These weights were trained on a V100 and may not perform the same on other GPUs.
