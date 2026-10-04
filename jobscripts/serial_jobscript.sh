#!/bin/bash

#SBATCH --nodes=1			# number of nodes
#SBATCH --ntasks-per-node=1		# tasks per node
#SBATCH --cpus-per-task=8		# cores per task
#SBATCH --gres=gpu:1			# GPUs per node
#SBATCH --time=00:30:00			# time limit (d-hh:mm:ss)
#SBATCH --account=tra26_poliex		# project account
#SBATCH --partition=boost_usr_prod	# partition name
#SBATCH --out=serial.out		# output file
#SBATCH --err=serial.err		# error file

module purge

module load gcc/12.2.0
module load cuda/12.2
module load cmake/3.27.9
module load hdf5/1.14.3--gcc--12.2.0-spack0.22
module load python/3.11.7

TARGET=serial
BUILD_DIR=$TARGET-build

cmake -B "$BUILD_DIR" \
	-DPARTICLES_NATIVE_ARCH=OFF

cmake --build "$BUILD_DIR"

./"$BUILD_DIR"/src/$TARGET/particles_$TARGET input/Particles.in output/$TARGET.hd5 1000
