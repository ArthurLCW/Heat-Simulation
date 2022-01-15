#!/bin/bash
#SBATCH --account=csc4005
#SBATCH --partition=debug
#SBATCH --qos=normal
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --output seq.out
#SBATCH --time=10



echo "mainmode: " && /bin/hostname
xvfb-run -a /pvfsmnt/118010134/csc4005-assignment-4-seq/csc4005-imgui/build/csc4005_imgui 1600