#!/bin/bash
#SBATCH -J random_pipeline
#SBATCH -o random_pipeline_%j.out
#SBATCH -e random_pipeline_%j.err
#SBATCH --gres=gpu:4
#SBATCH --gpus-per-node=4
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --mem=0
#SBATCH --time=10:00:00
#SBATCH --exclusive
ulimit -s unlimited
## Number of total processes
echo " "
echo " Nodelist:= " $SLURM_JOB_NODELIST
echo " Number of nodes:= " $SLURM_JOB_NUM_NODES
echo " GPUs per node:= " $SLURM_JOB_GPUS
echo " Ntasks per node:= "  $SLURM_NTASKS_PER_NODE
echo " Running on multiple nodes/GPU devices"
echo ""
echo " Run started at:- "
date
ENABLE_BEAM_SEARCH=0 OPTIMIZE="-O3" bash generate_autotune_results.sh 125 0 0 0 0 random_pipeline
echo "Run completed at:- "
date

