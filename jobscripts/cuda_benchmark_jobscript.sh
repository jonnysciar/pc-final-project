#!/bin/bash

#SBATCH --nodes=1			# number of nodes
#SBATCH --ntasks-per-node=1		# tasks per node
#SBATCH --cpus-per-task=8		# cores per task
#SBATCH --gres=gpu:1			# GPUs per node
#SBATCH --time=02:00:00			# time limit (d-hh:mm:ss)
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
	-DPARTICLES_FMA_CUDA=OFF

cmake --build "$BUILD_DIR"

# 2K
for i in $(seq 1 5);
do
	SIZE=2K
	INPUT=input/Particles$SIZE.in

	# Basic
	./"$BUILD_DIR"/src/cuda/particles_cuda_basic $INPUT none 0 > benchmarks/cuda/cuda_basic$SIZE-$i.out
	# IntegrateVV
	./"$BUILD_DIR"/src/cuda/particles_cuda_integrateVV $INPUT none 0 > benchmarks/cuda/cuda_integrateVV$SIZE-$i.out

	# Shared
	./"$BUILD_DIR"/src/cuda/particles_cuda_shared $INPUT none 0 > benchmarks/cuda/cuda_shared$SIZE-$i.out
done
echo "Finish 2K!"

# 16K
for i in $(seq 1 5);
do
	SIZE=16K
	INPUT=input/Particles$SIZE.in

	# Basic
	./"$BUILD_DIR"/src/cuda/particles_cuda_basic $INPUT none 0 > benchmarks/cuda/cuda_basic$SIZE-$i.out
	# IntegrateVV
	./"$BUILD_DIR"/src/cuda/particles_cuda_integrateVV $INPUT none 0 > benchmarks/cuda/cuda_integrateVV$SIZE-$i.out

	# Shared
	./"$BUILD_DIR"/src/cuda/particles_cuda_shared $INPUT none 0 > benchmarks/cuda/cuda_shared$SIZE-$i.out
done
echo "Finish 16K!"

# 60K
for i in $(seq 1 5);
do
	SIZE=60K
	INPUT=input/Particles$SIZE.in

	# Basic
	./"$BUILD_DIR"/src/cuda/particles_cuda_basic $INPUT none 0 > benchmarks/cuda/cuda_basic$SIZE-$i.out
	# IntegrateVV
	./"$BUILD_DIR"/src/cuda/particles_cuda_integrateVV $INPUT none 0 > benchmarks/cuda/cuda_integrateVV$SIZE-$i.out

	# Shared
	./"$BUILD_DIR"/src/cuda/particles_cuda_shared $INPUT none 0 > benchmarks/cuda/cuda_shared$SIZE-$i.out
done
echo "Finish 60K!"

# 350K
for i in $(seq 1 5);
do
        SIZE=350K
        INPUT=input/Particles$SIZE.in

        # Basic
        ./"$BUILD_DIR"/src/cuda/particles_cuda_basic $INPUT none 0 > benchmarks/cuda/cuda_basic$SIZE-$i.out
        # IntegrateVV
        ./"$BUILD_DIR"/src/cuda/particles_cuda_integrateVV $INPUT none 0 > benchmarks/cuda/cuda_integrateVV$SIZE-$i.out

        # Shared
        ./"$BUILD_DIR"/src/cuda/particles_cuda_shared $INPUT none 0 > benchmarks/cuda/cuda_shared$SIZE-$i.out
done
echo "Finish 350K!"


echo "Finish all!"
