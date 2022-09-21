colon := :
$(colon) := :

all: overthrust_3D_initial_model.h5 reverse

overthrust_3D_initial_model.h5:
	wget ftp://slim.gatech.edu/data/SoftwareRelease/WaveformInversion.jl/3DFWI/overthrust_3D_initial_model.h5

reverse: overthrust_3D_initial_model.h5 overthrust_experiment.py
	rm -rf data/nvme*/*
	DEVITO_OPT=advanced \
	DEVITO_LANGUAGE=openmp \
	DEVITO_PLATFORM=skx \
	OMP_NUM_THREADS=26 \
	OMP_PLACES="{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25}" \
	DEVITO_LOGGING=DEBUG \
	time numactl --cpubind=0  python overthrust_experiment.py --disks=8

rtm: rtm.py
	DEVITO_OPT=advanced \
	DEVITO_LANGUAGE=openmp \
	DEVITO_PLATFORM=skx \
	OMP_NUM_THREADS=26 \
	OMP_PLACES="{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25}" \
	DEVITO_LOGGING=DEBUG \
	TMPDIR=/home/ubuntu/overthrust-tests/RTM \
	time numactl --cpubind=0  python rtm.py

compression: overthrust_3D_initial_model.h5 overthrust_experiment.py
	rm -rf data/nvme*/*
	DEVITO_OPT=advanced \
	DEVITO_LANGUAGE=openmp \
	DEVITO_PLATFORM=skx \
	OMP_NUM_THREADS=26 \
	OMP_PLACES="{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25}" \
	DEVITO_LOGGING=DEBUG \
	time numactl --cpubind=0  python overthrust_experiment.py --compression --rate=16 --disks=8

reverse-mpi: overthrust_3D_initial_model.h5 overthrust_experiment.py overthrust_experiment.py
	rm -rf data/nvme*/*
	DEVITO_OPT=advanced \
	DEVITO_LANGUAGE=openmp \
	DEVITO_MPI=1 \
	OMP_NUM_THREADS=26 \
	OMP_PLACES="{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51}" \
	DEVITO_LOGGING=DEBUG \
	time mpirun --map-by socket --bind-to socket -np 2 python overthrust_experiment.py --mpi --disks=8

gradient: overthrust_3D_initial_model.h5 test_gradient.py
	rm -rf data/nvme*/*
	DEVITO_OPT=advanced \
	DEVITO_LANGUAGE=openmp \
	DEVITO_PLATFORM=skx \
	DEVITO_JIT_BACKDOOR=1 \
	OMP_NUM_THREADS=26 \
	OMP_PLACES="{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25}" \
	TMPDIR=/home/ubuntu/overthrust-tests/GRADIENT_C_DEVITO \
	DEVITO_LOGGING=DEBUG \
	time numactl --cpubind=0  python test_gradient.py