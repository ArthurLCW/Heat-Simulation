#!/bin/bash
#SBATCH --account=csc4005
#SBATCH --partition=debug
#SBATCH --qos=normal
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --output data200.out
#SBATCH --time=10



echo "mainmode: " && /bin/hostname
srun xvfb-run -a /pvfsmnt/118010134/csc4005-assignment-4-cuda/csc4005-imgui/build/csc4005_imgui 4 200