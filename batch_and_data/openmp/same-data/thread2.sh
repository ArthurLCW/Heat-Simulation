#!/bin/bash
#SBATCH --account=csc4005
#SBATCH --partition=debug
#SBATCH --qos=normal
#SBATCH --ntasks=2
#SBATCH --nodes=1
#SBATCH --output thread2.out
#SBATCH --time=10



echo "mainmode: " && /bin/hostname
xvfb-run -a /pvfsmnt/118010134/csc4005-assignment-4-omp/csc4005-imgui/build/csc4005_imgui 2 1600