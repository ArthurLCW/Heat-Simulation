#!/bin/bash
#SBATCH --account=csc4005
#SBATCH --partition=debug
#SBATCH --qos=normal
#SBATCH --ntasks=4
#SBATCH --nodes=1
#SBATCH --output data400.out
#SBATCH --time=10



echo "mainmode: " && /bin/hostname
xvfb-run -a mpirun /pvfsmnt/118010134/csc4005-assignment-4-mpi/csc4005-imgui/build/csc4005_imgui 400