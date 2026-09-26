#!/bin/bash

#SBATCH --nodes=1			# number of nodes
#SBATCH --ntasks-per-node=1		# tasks per node
#SBATCH --cpus-per-task=8		# cores per task
#SBATCH --gres=gpu:1			# GPUs per node
#SBATCH --time=00:30:00			# time limit (d-hh:mm:ss)
#SBATCH --account=tra26_poliex		# project account
#SBATCH --partition=boost_usr_prod	# partition name
#SBATCH --out=cuda.out			# output file
#SBATCH --err=cuda.err			# error file

module purge

module load gcc/12.2.0
module load cuda/12.2
module load cmake/3.27.9
module load hdf5/1.14.3--gcc--12.2.0-spack0.22
module load python/3.11.7

BUILD_DIR=cuda-build

cmake -B "$BUILD_DIR" \
	-DPARTICLES_BUILD_CUDA=ON \
	-DPARTICLES_FAST_MATH_CUDA=ON \
	-DPARTICLES_FMA_CUDA=ON \

cmake --build "$BUILD_DIR"

./"$BUILD_DIR"/src/cuda/particles_cuda input/Particles.in output/cuda.hd5 1000
