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


mkdir -p profile/cuda

function do_nsys {
	SIZE=$1
	TYPE=$2
	INPUT=input/Particles$SIZE.in
	OUTPUT=profile/cuda

	nsys profile --stats=true --trace=cuda,nvtx,osrt \
		-f true -o $OUTPUT/nsys_$TYPE$SIZE \
		./"$BUILD_DIR"/src/cuda/particles_cuda_$TYPE \
		$INPUT none 0
}

function do_ncu {
	SIZE=$1
	TYPE=$2
	INPUT=input/Particles$SIZE.in
	OUTPUT=profile/cuda

	ncu --set full --kernel-name computeForcesGPU --launch-count 1 \
		-f -o $OUTPUT/ncu_$TYPE$SIZE \
		./"$BUILD_DIR"/src/cuda/particles_cuda_$TYPE \
		$INPUT none 0
}


for SIZE in 2K 16K 60K 350K;
do

	for TYPE in basic integrateVV shared;
	do	
		do_nsys $SIZE $TYPE
		do_ncu $SIZE $TYPE
	done

	echo "Finish $SIZE!"
done


cmake -B "$BUILD_DIR" \
        -DPARTICLES_BUILD_CUDA=ON \
        -DPARTICLES_FAST_MATH_CUDA=ON \
        -DPARTICLES_FMA_CUDA=ON

cmake --build "$BUILD_DIR"

for SIZE in 2K 16K 60K 350K;
do
	do_nsys $SIZE fma
	do_ncu $SIZE fma

	echo "Finish fma $SIZE!"
done

echo "Finish all!"
