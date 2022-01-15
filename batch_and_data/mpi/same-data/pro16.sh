#!/bin/bash
#SBATCH --account=csc4005
#SBATCH --partition=debug
#SBATCH --qos=normal
#SBATCH --ntasks=16
#SBATCH --nodes=1
#SBATCH --output pro16.out
#SBATCH --time=10



echo "mainmode: " && /bin/hostname
xvfb-run -a mpirun /pvfsmnt/118010134/csc4005-assignment-4-mpi/csc4005-imgui/build/csc4005_imgui 1600